// The career race flow shared by the copies (race.h, docs/RE-CORRIDA.txt).
#include <cstdio>
#include <algorithm>
#include <limits>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>

#include "car.h"
#include "mod.h"
#include "race.h"
#include "ai.h"

namespace mod::race {

namespace {

constexpr uint32_t kCareer = 0x557F0C;       // mcPgCareer*: +0x0C career data, +0x2C next state, +0x34 u16 race id
constexpr uint32_t kRaceMachine = 0x557EEC;  // +4 race state: 5 go, 6 racing, 7 finished, 11 autosave, 12 over
constexpr uint32_t kRaceHolder = 0x533E24;   // [+4] the current race object (the cruise in free roam)
constexpr uint32_t kGameState = 0x557ED0;    // +4 == 3: a state that skips the race end and the credit
constexpr uint32_t kSetupCurrent = 0x53F2E0;
constexpr uint32_t kRaceDb = 0x557EE4;       // per city 0x50: +0x18 race names, +0x20 descriptors (0x30, start +0x14)
constexpr uint32_t kCruiseVtable = 0x46A7D8;
constexpr uint32_t kRecordVtable = 0x473E74;
constexpr uint32_t kRecord = 0x4C;
constexpr uint32_t kEntry = 0x98, kEntrySize = 0xB8;  // racer i at race + 0x98 + i * 0xB8

bool Ptr(uint32_t va) { return va >= 0x10000 && Mapped(va, 4); }
uint32_t P(uint32_t va) { return Ptr(va) ? R32(va) : 0; }

// a race is challenged with the headlights on: the marker test needs the local car's light flag (model +0x132, what
// the light button toggles through 0xF6BD0)
void Headlights() {
    uint32_t model = P(P(P(P(0x558814) + 8) + 0x14) + 0x0C);
    if (Ptr(model + 0x132)) G<uint8_t>(model + 0x132) = 1;
}

std::mutex g_lock;
// host
uint32_t g_serial = 0, g_sent_id = 0;
bool g_out_start = false;
Start g_start_out{};
// partner
bool g_have_start = false, g_start_done = false;
Start g_start_in{};
uint32_t g_done_serial = 0;
int g_phase = 0;  // 0 idle, 1 car outside the marker (it arms), 2 in it with the lights on, waiting for the trigger
float g_target[3] = {};
uint32_t g_record_id = 0;
uint8_t g_saved_hookman = 0xFF, g_saved_side = 0xFF;
// the partner's own record of the host's race, its opponents and difficulty (+0x0B..+0x4B) taken from the host's
// record for this start (the career race build 0x277AE0 reads them at the load) and given back once it has loaded
uint8_t g_saved_fields[0x4C]{};
bool g_fields_lent = false;
uint16_t g_fields_id = 0;
uint64_t g_phase_at = 0;
// both
uint64_t g_lights_until = 0;  // the test's challenge: the lights kept on until then
bool g_partner_won = false, g_out_won = false, g_won_sent = false;
uint32_t g_won_serial = 0, g_race_serial = 0;
// the prize: the winner sends what it was paid (1st place), a copy credited with the partner's win tops its own up
int32_t g_partner_prize = 0, g_out_prize = 0;
bool g_prize_sent = false;
uint32_t g_unsupported_race = 0, g_bound_race = 0;
std::string g_last;
bool g_challenge_input = false;
Setup g_setup_out{}, g_setup_in{};
uint64_t g_setup_send_at = 0;
bool g_have_setup = false;
uint32_t g_setup_applied_revision = 0;
// Common two-human races keep ReportRaceResult pending until the peer result is known.
enum class GatePhase { Off, Guarding, Waiting, Replay, Done, Blocked };
GatePhase g_gate = GatePhase::Off;
bool g_gate_requested = false, g_gate_resolved = false, g_gate_shared_won = false;
uint32_t g_gate_race = 0;
uint64_t g_gate_at = 0, g_finished_send_at = 0;
bool g_local_finished = false, g_peer_finished = false;
Finished g_finished_out{}, g_finished_peer{};
float g_gate_finish_time = 0, g_gate_last_lap = 0;
const char* g_gate_reason = "off";
constexpr uint64_t kFinishWaitMs = 15000, kReplayWaitMs = 2500;

void BeginRace(uint32_t serial) {
    g_race_serial = serial;
    g_partner_won = g_out_won = g_won_sent = g_prize_sent = false;
    g_partner_prize = g_out_prize = 0;
    g_bound_race = 0;
    g_setup_out = g_setup_in = {};
    g_setup_send_at = 0;
    g_have_setup = false;
    g_setup_applied_revision = 0;
    const char* opt = std::getenv("MC3COOP_FINISH_GATE");
    g_gate_requested = !opt || std::strcmp(opt, "0") != 0;
    g_gate = GatePhase::Off;
    g_gate_race = 0;
    g_gate_at = g_finished_send_at = 0;
    g_gate_resolved = g_gate_shared_won = g_local_finished = g_peer_finished = false;
    g_finished_out = g_finished_peer = {};
    g_gate_reason = g_gate_requested ? "awaiting_supported_race" : "off";
}

uint32_t Cruise() {
    uint32_t race = P(P(kRaceHolder) + 4);
    return race && P(race) == kCruiseVtable ? race : 0;
}

int RaceState() { return int(P(P(kRaceMachine) + 4)); }

// the open-race record of this copy with that id, or of that (City, RaceIdx)
uint32_t FindRecord(uint32_t data, int id, int city, int race_idx) {
    uint32_t begin = P(data + 0xA5C), end = P(data + 0xA60);
    if (!begin || end < begin || end - begin > 0x200 * kRecord) return 0;
    for (uint32_t r = begin; r + kRecord <= end; r += kRecord) {
        if (!Mapped(r, kRecord)) return 0;
        if (id >= 0 ? G<uint16_t>(r + 0x04) == id : (G<int8_t>(r + 0x08) == city && G<int16_t>(r + 0x06) == race_idx))
            return r;
    }
    return 0;
}

const char* RaceName(int city, int race_idx) {
    uint32_t names = P(P(kRaceDb) + uint32_t(city) * 0x50 + 0x18);
    uint32_t name = P(names + uint32_t(race_idx) * 4);
    return name && Mapped(name, 32) ? static_cast<const char*>(HostPtr(name)) : "";
}

// Setup+68 is an inline filename, written by 26B8E0 before 28D580 finishes the opponent mask.
// Match the full local database name, rather than Start.name's shortened display label.
bool SetupMatches(uint32_t setup, int city, int race_idx) {
    if (!Mapped(setup, 0x3D8) || int(G<int8_t>(setup)) != city) return false;
    const char* expected = RaceName(city, race_idx);
    if (!*expected) return false;
    for (uint32_t i = 0; i < 128; i++) {
        if (!Mapped(uint32_t(reinterpret_cast<const uint8_t*>(expected) - g_base) + i, 1)) return false;
        uint8_t actual = G<uint8_t>(setup + 0x68 + i);
        if (actual != uint8_t(expected[i])) return false;
        if (!actual) return true;
    }
    return false;
}

void CaptureHostSetup() {
    if (!g_race_serial || g_start_out.serial != g_race_serial) return;
    uint32_t next = P(0x53F2E4);
    if (!SetupMatches(next, g_start_out.city, g_start_out.race_idx)) return;
    uint32_t mask = R32(next + 0x3D4), limit = R32(next + 0x40);
    if (limit > 100) return; // 28D740 uses 100 as the unrestricted sentinel.
    uint8_t opponents[8][0x2C]{};
    for (uint32_t i = 0; i < 8; i++) {
        uint32_t s = next + 0x274 + i * 0x2C;
        int32_t model = int32_t(R32(s));
        if (model < -1 || model >= 127 || G<uint8_t>(s + 4) > 1) return;
        std::memcpy(opponents[i], HostPtr(s), 0x29); // exclude the three native padding bytes
        opponents[i][0x27] = 0; // bound the inline name (35 bytes beginning at +5)
    }
    if (g_setup_out.serial && g_setup_out.opponent_mask == mask && g_setup_out.opponent_limit == limit &&
        !std::memcmp(g_setup_out.opponents, opponents, sizeof opponents)) return;
    uint32_t revision = g_setup_out.revision + 1;
    g_setup_out = {g_race_serial, revision, mask, limit, g_start_out.city, 0, g_start_out.race_idx, {}};
    std::memcpy(g_setup_out.opponents, opponents, sizeof opponents);
    g_setup_send_at = 0;
    Log("race: host setup serial %u revision %u mask=%08X limit=%u", g_race_serial, revision, mask, limit);
}

void ApplyHostSetup() {
    if (!g_have_setup || g_setup_in.serial != g_race_serial) return;
    // Once players/opponents exist, changing this mask cannot change the already allocated race.
    if (!Cruise() && RaceState() >= 2) return;
    bool wrote = false;
    for (uint32_t global : {kSetupCurrent, uint32_t(0x53F2E4)}) {
        uint32_t setup = P(global);
        if (!SetupMatches(setup, g_setup_in.city, g_setup_in.race_idx)) continue;
        W32(setup + 0x3D4, g_setup_in.opponent_mask);
        W32(setup + 0x40, g_setup_in.opponent_limit);
        std::memcpy(HostPtr(setup + 0x274), g_setup_in.opponents, sizeof g_setup_in.opponents);
        wrote = true;
    }
    if (wrote && g_setup_applied_revision != g_setup_in.revision) {
        g_setup_applied_revision = g_setup_in.revision;
        Log("race: applied host setup revision %u mask=%08X limit=%u before opponent allocation",
            g_setup_in.revision, g_setup_in.opponent_mask, g_setup_in.opponent_limit);
    }
}

// the host: its cruise just started an open race
void HostTick() {
    uint32_t career = P(kCareer), data = P(career + 0x0C);
    if (!data || !Cruise()) return;
    uint32_t next = P(career + 0x2C), id = G<uint16_t>(career + 0x34);
    if (next != 4 || !id) {
        g_sent_id = 0;
        return;
    }
    if (id == g_sent_id) return;
    uint32_t rec = FindRecord(data, int(id), 0, 0);
    if (!rec) return;
    g_sent_id = id;
    Start s{};
    s.serial = ++g_serial;
    s.city = G<int8_t>(rec + 0x08);
    s.race_idx = G<int16_t>(rec + 0x06);
    std::memcpy(s.record, HostPtr(rec), kRecord);
    std::snprintf(s.name, sizeof s.name, "%s", RaceName(s.city, s.race_idx));
    g_start_out = s;
    g_out_start = true;
    BeginRace(s.serial);
    ai::SetRace(s.serial, s.city, s.race_idx, true);
    Log("race: host starts %s (city %d race %d)", s.name, s.city, s.race_idx);
}

// the partner: the host's race in this copy's open list (as AddOpenRace 0x283290 does it), its id
uint32_t EnsureRecord(uint32_t data, const Start& s) {
    if (uint32_t r = FindRecord(data, -1, s.city, s.race_idx)) return r;
    uint32_t end = P(data + 0xA60), cap = P(data + 0xA64);
    if (!end || cap < end + kRecord || !Mapped(end, kRecord)) {
        Log("race: no room in the open-race list to add %s", s.name);
        return 0;
    }
    uint8_t rec[kRecord];
    std::memcpy(rec, s.record, kRecord);
    uint16_t id = uint16_t(G<uint16_t>(data + 0xB18) + 1);
    if (!id) id = 1;
    G<uint16_t>(data + 0xB18) = id;
    uint32_t vt = kRecordVtable;
    std::memcpy(rec + 0x00, &vt, 4);
    std::memcpy(rec + 0x04, &id, 2);
    rec[0x09] = 0xFF;  // no hookman of this copy's (its OpenHookmen may not hold the host's)
    rec[0x0A] = 0xFF;  // no side quest (per-city counters would underflow)
    // Task* and user-name slots belong to the sender's career allocation, never this one.
    std::memset(rec + 0x1E, 0, 2);
    std::memset(rec + 0x20, 0, 4);
    if (rec[0x26] > 8) return 0;
    for (unsigned i = 0; i < 8; i++) {
        uint8_t* cfg = rec + 0x34 + i * 3;
        if (int8_t(cfg[0]) >= 0 && int8_t(cfg[1]) <= -2) {
            cfg[0] = cfg[1] = cfg[2] = 0xFF;
            rec[0x27] = 0; // let the recipient generate its own valid local name references
        }
    }
    rec[0x24] = 0;     // not 'new': no popup
    std::memcpy(HostPtr(end), rec, kRecord);
    G<uint16_t>(data + 0x5A + (uint32_t(s.city) * 0xA0 + uint32_t(s.race_idx)) * 2) += 1;
    W32(data + 0xA60, end + kRecord);
    Log("race: added the host's race %s to this career (id %u)", s.name, id);
    return end;
}

void PartnerTick(uint64_t now) {
    uint32_t career = P(kCareer), data = P(career + 0x0C);
    if (!data) return;
    if (g_fields_lent && !Cruise() && RaceState() >= 2 && RaceState() <= 6) {  // the race has loaded: as it was
        if (uint32_t rec = FindRecord(data, g_fields_id, 0, 0)) {
            uint8_t hook = G<uint8_t>(rec + 0x09), side = G<uint8_t>(rec + 0x0A);
            std::memcpy(HostPtr(rec + 0x0B), g_saved_fields + 0x0B, kRecord - 0x0B);
            G<uint8_t>(rec + 0x09) = hook, G<uint8_t>(rec + 0x0A) = side;
        }
        g_fields_lent = false;
    }
    if (g_phase == 1 && now - g_phase_at > 300) {  // armed outside: into the marker
        car::Pose at;
        if (car::Local(at)) {
            for (int i = 0; i < 3; i++) at.m[3][i] = g_target[i];
            car::Place(at);
        }
        g_phase = 2;
    }
    if (g_phase >= 1) {  // waiting for this copy's own trigger
        Headlights();
        uint32_t rec = FindRecord(data, int(g_record_id), 0, 0);
        bool started = P(career + 0x2C) == 4 && G<uint16_t>(career + 0x34) == g_record_id;
        if (started || now - g_phase_at > 10000) {
            if (rec) G<uint8_t>(rec + 0x09) = g_saved_hookman, G<uint8_t>(rec + 0x0A) = g_saved_side;  // as it was
            g_phase = 0;
            g_start_done = true;
            g_done_serial = g_start_in.serial;
            g_race_serial = g_start_in.serial;
            Log(started ? "race: started the host's race here" : "race: the host's race did not start here");
        }
        return;
    }
    if (!g_have_start || g_start_in.serial == g_done_serial) return;
    uint32_t cruise = Cruise();
    if (!cruise || RaceState() >= 7 || G<uint8_t>(cruise + 0x0D)) return;  // not in free roam now: later
    const Start& s = g_start_in;
    if (int(G<int8_t>(P(kSetupCurrent) + 0x00)) != s.city) {
        Log("race: the host's race is in city %d, this copy is in another", s.city);
        g_done_serial = s.serial;
        return;
    }
    uint32_t rec = EnsureRecord(data, s);
    if (!rec) {
        g_done_serial = s.serial;
        return;
    }
    g_record_id = G<uint16_t>(rec + 0x04);
    if (std::memcmp(HostPtr(rec + 0x0B), s.record + 0x0B, kRecord - 0x0B)) {  // same race, the host's opponents
        std::memcpy(g_saved_fields, HostPtr(rec), kRecord);
        uint16_t task = G<uint16_t>(rec + 0x1E);
        uint32_t task_name = R32(rec + 0x20);
        std::memcpy(HostPtr(rec + 0x0B), s.record + 0x0B, kRecord - 0x0B);
        G<uint16_t>(rec + 0x1E) = task;                                 // this career's own task
        W32(rec + 0x20, task_name);                                     // never borrow a host pointer
        G<uint8_t>(rec + 0x24) = G<uint8_t>(rec + 0x24) & ~uint8_t(0x10);  // not 'new'
        g_fields_lent = true, g_fields_id = uint16_t(g_record_id);
        Log("race: this career's record of the race takes the host's opponents for this start");
    }
    g_saved_hookman = G<uint8_t>(rec + 0x09), g_saved_side = G<uint8_t>(rec + 0x0A);
    G<uint8_t>(rec + 0x09) = 0xFF, G<uint8_t>(rec + 0x0A) = 0xFF;  // the marker filter: any car class, no roaming hookman
    G<uint8_t>(cruise + 0x1250) = 1;                               // armed
    W32(cruise + 0x1258, 0xFFFFFFFF), W32(cruise + 0x125C, 0xFFFFFFFF);
    uint32_t desc = P(P(kRaceDb) + uint32_t(s.city) * 0x50 + 0x20) + uint32_t(s.race_idx) * 0x30;
    if (!Ptr(desc + 0x1C)) {
        g_done_serial = s.serial;
        return;
    }
    car::Pose at;
    if (!car::Local(at)) return;
    g_target[0] = RF(desc + 0x14), g_target[1] = RF(desc + 0x18) + 0.5f, g_target[2] = RF(desc + 0x1C);
    at.m[3][0] = g_target[0] + 60.0f, at.m[3][1] = g_target[1], at.m[3][2] = g_target[2];  // outside first: it arms
    car::Place(at);
    G<uint8_t>(cruise + 0x1250) = 0;
    g_phase = 1;
    g_phase_at = now;
    Log("race: taken to the start of %s", s.name);
}

// This counter belongs to the common lap/checkpoint race family only. Other modes use these bytes differently.
bool HasHumanFinishCounter(uint32_t race) {
    switch (P(race)) {
        case 0x46B2D8: case 0x46B4B8: case 0x46ABD8: case 0x46B188: return true;
        default: return false;
    }
}

bool GateOwns(uint32_t race) { return g_gate_race == race && g_gate != GatePhase::Off; }

bool GateSetup(uint32_t race) {
    uint32_t setup = P(kSetupCurrent), players = P(0x558814), session = P(0x557EFC);
    return P(race) == 0x46B2D8 && P(setup + 0x4C) == 2 && P(players + 0x28) == 2 &&
           !G<uint8_t>(setup + 0x108) && Ptr(session) && P(session + 0x10) == 0xFFFFFFFF &&
           P(setup + 0x50) == 1 && P(setup + 0x128) == 0 && P(setup + 0x154) == 0xFFFFFFFF &&
           Ptr(P(P(players + 8) + 0x58)) && !G<uint8_t>(race + 0x6F4);
}

void ConfigureGate(uint32_t race, int state) {
    if (!g_gate_requested || !g_race_serial || g_gate != GatePhase::Off || g_gate_race ||
        state < 2 || state > 5 || G<uint8_t>(race + kEntry + 4)) return;
    if (!GateSetup(race) || R32(race + 0x1250) != 0 || G<uint8_t>(race + 0x0D) || G<uint8_t>(race + 0x0E)) {
        g_gate_reason = "unsupported_setup_or_already_counted";
        return;
    }
    int count = int(P(race + 0x24));
    if (count < 1 || count > 64 || !Mapped(P(race + 0x28), uint32_t(count) * 0x120)) {
        g_gate_reason = "unsupported_checkpoints";
        return;
    }
    g_gate_race = race;
    g_gate = GatePhase::Guarding;
    g_gate_reason = "remote_guarded_not_counted";
    Log("race: EXPERIMENT finish gate armed, serial %u", g_race_serial);
}

// Capture before shared-win writes. Losses are as important as wins: otherwise both losing copies deadlock.
bool ValidFinished(const Finished& finished) {
    return finished.serial && finished.won <= 1 && finished.dnf <= 1 && finished.arrested <= 1 &&
           !finished.reserved && !(finished.won && finished.dnf) &&
           std::isfinite(finished.finish_time) && finished.finish_time >= 0 && finished.finish_time <= 86400.f &&
           std::isfinite(finished.progress) && std::fabs(finished.progress) <= 1.0e9f;
}

void ObserveLocalFinish(uint32_t race, int state, uint64_t now) {
    uint32_t local = race + kEntry;
    if (!g_race_serial || g_local_finished || state < 5 || state > 10 || !G<uint8_t>(local + 4)) return;
    Finished finished{g_race_serial, uint8_t(G<uint8_t>(local + 0x5D) != 0), uint8_t(G<uint8_t>(local + 6) != 0),
                      uint8_t(G<uint8_t>(local + 5) != 0), 0, RF(local + 0x10), RF(local + 0x3C),
                      int32_t(R32(local + 0x44))};
    if (!ValidFinished(finished)) {
        g_gate_reason = "invalid_local_terminal";
        return;
    }
    g_local_finished = true;
    g_finished_out = finished;
    g_finished_send_at = 0;
    if (GateOwns(race) && g_gate == GatePhase::Guarding) {
        g_gate_finish_time = RF(local + 0x10);
        g_gate_last_lap = RF(local + 0x24);
        g_gate = GatePhase::Waiting;
        g_gate_at = now;
        g_gate_reason = "awaiting_peer_terminal";
        // The terminal result is genuine, independently of an earlier peer win message.
        if (g_finished_out.won && !g_won_sent) {
            g_won_sent = g_out_won = true;
            g_won_serial = g_race_serial;
            g_out_prize = 0;
        }
    }
}

bool GateReplayPose(uint32_t race, car::Pose& pose) {
    int count = int(P(race + 0x24));
    if (count < 1 || count > 64 || !Mapped(P(race + 0x28), uint32_t(count) * 0x120) || !car::Local(pose)) return false;
    uint32_t cp = P(race + 0x28) + uint32_t(count - 1) * 0x120;
    for (int i = 0; i < 3; i++) {
        float coord = RF(cp + 0xFC + uint32_t(i) * 4);
        if (!std::isfinite(coord)) return false;
        pose.m[3][i] = coord;
    }
    pose.m[3][1] += 0.5f;
    return true;
}

void GateForfeit(uint32_t race, int state) {
    uint32_t local = race + kEntry, players = P(0x558814), setup = P(kSetupCurrent);
    // This is only a fallback for the local player, whose HUD exists. The peer is still callback-protected.
    if (state == 6 && P(race) == 0x46B2D8 && P(setup + 0x4C) == 2 && P(players + 0x28) == 2 &&
        !G<uint8_t>(setup + 0x108) && Ptr(P(P(players + 8) + 0x58)) && R32(race + 0x1250) == 1 &&
        G<uint8_t>(race + kEntry + kEntrySize + 4) && !G<uint8_t>(race + 0x0D) && !G<uint8_t>(race + 0x0E)) {
        g_gate_resolved = true;
        g_gate_shared_won = g_partner_won = false;
        g_partner_prize = 0;
        W32(local + 0x18, 0);
        G<uint8_t>(local + 0x5D) = 0;
        G<float>(local + 0x0C) = 0;
        G<uint8_t>(local + 4) = 0;
        G<uint8_t>(race + 0x6F4) = 1;
        g_gate_reason = "replay_failed_local_forfeit";
    } else g_gate_reason = "replay_failed_guard_blocked";
    g_gate = GatePhase::Blocked;
    Log("race: EXPERIMENT finish gate %s", g_gate_reason);
}

void AdvanceGate(uint32_t race, int state, uint64_t now, bool connected) {
    if (!GateOwns(race)) return;
    uint32_t local = race + kEntry;
    if (state >= 7) {
        g_gate = GatePhase::Done;
        g_gate_reason = "native_result_processed";
        return;
    }
    if (g_gate == GatePhase::Waiting) {
        bool timed_out = now - g_gate_at >= kFinishWaitMs;
        if (!g_finished_out.won && !g_peer_finished && !g_partner_won && connected && !timed_out) return;
        // Validate every dependency BEFORE rearming or moving the local player.
        car::Pose pose;
        if (state != 6 || !GateSetup(race) || R32(race + 0x1250) != 1 ||
            G<uint8_t>(race + 0x0D) || G<uint8_t>(race + 0x0E) || !G<uint8_t>(local + 4) ||
            G<uint8_t>(local + 6) || R32(local + 0x18) != 0 || !G<uint8_t>(local + 7) ||
            !std::isfinite(g_gate_finish_time) || !std::isfinite(g_gate_last_lap) ||
            g_gate_finish_time <= 0 || g_gate_last_lap <= 0 || g_gate_last_lap > g_gate_finish_time ||
            !GateReplayPose(race, pose)) {
            GateForfeit(race, state);
            return;
        }
        g_gate_shared_won = g_finished_out.won || g_partner_won || (g_peer_finished && g_finished_peer.won);
        g_gate_resolved = true;
        g_gate = GatePhase::Replay;
        g_gate_at = now;
        g_gate_reason = !connected ? "replay_disconnected" : timed_out ? "replay_timeout_local_result" : "replay_terminal_result";
        uint32_t last = P(race + 0x24) - 1;
        // +1250 stays 1: reinterpret that single count as the protected remote, then LOCAL counts 1 -> 2.
        W32(local + 0x18, 1);
        G<uint16_t>(local + 0x34) = uint16_t(last);
        G<uint8_t>(local + 0x48) = G<uint8_t>(local + 0x49) = uint8_t(last);
        for (uint32_t word = 0; word < 2; word++) {
            uint32_t bits = last >= (word + 1) * 32 ? 0xFFFFFFFFu :
                            last <= word * 32 ? 0u : (1u << (last - word * 32)) - 1;
            W32(local + 0x4C + word * 4, bits);
            W32(local + 0x54 + word * 4, bits);
        }
        // Prevent a millisecond lap from becoming a permanent best-lap record on the second callback.
        G<float>(local + 0x1C) = g_gate_finish_time - g_gate_last_lap;
        G<uint8_t>(local + 0x5D) = uint8_t(g_gate_shared_won);
        G<uint8_t>(local + 4) = 0; // publish last; the next native checkpoint event is local, with a valid HUD
        car::Place(pose);
        Log("race: EXPERIMENT finish gate replay, shared win %d (%s)", g_gate_shared_won, g_gate_reason);
    } else if (g_gate == GatePhase::Replay && now - g_gate_at >= kReplayWaitMs) {
        // Never keep rearming, reset permanent guards, or synthesize career counters if native replay fails.
        // A local time-up is safe with its real HUD and conservatively reports defeat (it clears win itself).
        GateForfeit(race, state);
    }
}

// Complete remote slots locally BEFORE the local player finishes, without 0x1E06E0 (it requires a HUD/camera).
// +0x18=0 stops the lap handler; +0x04 stops timer/finish callbacks. DNF keeps a zero-time placeholder from
// outranking the real local racer. This is bookkeeping in this world, not the partner's actual network result.
void CompleteRemoteRacers(uint32_t race, int state) {
    if (state < 2 || state > 6 || G<uint8_t>(race + kEntry + 4)) return;
    if (!HasHumanFinishCounter(race)) {
        if (g_unsupported_race != race) {
            g_unsupported_race = race;
            Log("race: remote completion unsupported for vtable %08X", P(race));
        }
        return;
    }
    uint32_t setup = P(kSetupCurrent), players = P(0x558814);
    int humans = int(P(setup + 0x4C));
    if (humans < 2 || humans > 8 || P(players + 0x28) != uint32_t(humans) ||
        !Mapped(race + kEntry, uint32_t(humans) * kEntrySize) || !Mapped(race + 0x1250, 12)) return;
    uint32_t finished = R32(race + 0x1250), added = 0, mask = 0;
    // Validate the entire operation before writing a partial counter update.
    for (int i = 1; i < humans; i++) {
        uint32_t entry = race + kEntry + uint32_t(i) * kEntrySize;
        uint32_t slot = setup + 0x114 + uint32_t(i) * 0x2C;
        if (!P(players + 8 + uint32_t(i) * 4) || int32_t(P(slot + 0x14)) != -1) continue;
        if (!G<uint8_t>(entry + 4)) added++;
    }
    if (finished >= uint32_t(humans) || added >= uint32_t(humans) - finished) return;
    for (int i = 1; i < humans; i++) {
        uint32_t entry = race + kEntry + uint32_t(i) * kEntrySize;
        uint32_t slot = setup + 0x114 + uint32_t(i) * 0x2C;
        if (!P(players + 8 + uint32_t(i) * 4) || int32_t(P(slot + 0x14)) != -1) continue;
        // Reapply the lap/CP guard: UpdateProgress can rewrite next_cp from the count every frame.
        W32(entry + 0x18, 0);
        G<uint16_t>(entry + 0x34) = 0xFFFF;
        G<uint8_t>(entry + 0x48) = uint8_t(std::clamp(int(P(race + 0x24)), 0, 255));
        for (uint32_t cp = 0x4C; cp <= 0x58; cp += 4) W32(entry + cp, 0xFFFFFFFF);
        G<uint8_t>(entry + 0x06) = 1;
        G<uint8_t>(entry + 0x5D) = 0;
        G<uint8_t>(entry + 0x04) = 1;
        mask |= 1u << i;
    }
    W32(race + 0x1258, R32(race + 0x1258) | mask);
    if (added && !GateOwns(race)) {
        W32(race + 0x1250, finished + added);
        Log("race: %u remote racer(s) completed locally, no HUD callback", added);
    }
}

bool TerminalRankContext(uint32_t race) {
    uint32_t setup = P(kSetupCurrent), players = P(0x558814);
    if (!Mapped(setup, 0x158) || !Mapped(players, 0x2C)) return false;
    return g_race_serial && g_bound_race == race && P(race) == 0x46B2D8 && P(setup + 0x4C) == 2 &&
           P(players + 0x28) == 2 && Ptr(P(players + 8)) && Ptr(P(players + 12)) &&
           !G<uint8_t>(setup + 0x108) && P(setup + 0x154) == 0xFFFFFFFF &&
           Mapped(race + kEntry, 2 * kEntrySize);
}

// The native rank comparator considers BOTH humans. A permanent local DNF placeholder would shift every AI
// rank when the actual peer won. Keep the no-callback guards/count, but import the peer's genuine rank context.
void ApplyPeerTerminal(uint32_t race) {
    if (!g_peer_finished || g_finished_peer.serial != g_race_serial || !ValidFinished(g_finished_peer) ||
        !TerminalRankContext(race)) return;
    uint32_t remote = race + kEntry + kEntrySize;
    if (!G<uint8_t>(remote + 4)) return;
    G<uint8_t>(remote + 5) = g_finished_peer.arrested;
    G<uint8_t>(remote + 6) = g_finished_peer.dnf;
    G<float>(remote + 0x10) = g_finished_peer.finish_time;
    G<float>(remote + 0x3C) = g_finished_peer.progress;
    W32(remote + 0x44, uint32_t(g_finished_peer.score));
    W32(remote + 0x18, 0);
}

void RestoreLocalTerminalTime(uint32_t race, int state) {
    if (state < 7 || state > 10 || g_gate != GatePhase::Done || !GateOwns(race) || !g_local_finished ||
        g_finished_out.serial != g_race_serial || !ValidFinished(g_finished_out) || !TerminalRankContext(race) ||
        !G<uint8_t>(race + 0x0D) || !G<uint8_t>(race + kEntry + 4)) return;
    // Report/statistics have already run. Restore only ranking time; never rewrite the global clock/best lap,
    // permanent guards, win flags or counters. Recorded statistics may still include the replay's network wait.
    G<float>(race + kEntry + 0x10) = g_finished_out.finish_time;
}

void RaceTick(bool connected) {
    uint32_t race = P(P(kRaceHolder) + 4);
    int state = RaceState();
    if (!race || Cruise() || !Mapped(race + kEntry, kEntrySize)) return;
    if (P(P(kGameState) + 4) == 3) return;
    uint64_t now = GetTickCount64();
    ConfigureGate(race, state);
    CompleteRemoteRacers(race, state);
    ApplyPeerTerminal(race);
    ObserveLocalFinish(race, state, now);
    AdvanceGate(race, state, now, connected);
    RestoreLocalTerminalTime(race, state);
    uint32_t local = race + kEntry;
    // With remotes already completed, the native local finish can go straight from 5 to 7 inside one call.
    // Observe it in the result states too; there may never be an xinput poll in state 6.
    if (state >= 5 && state <= 10 && G<uint8_t>(local + 4) && G<uint8_t>(local + 0x5D) &&
        !g_won_sent && !g_partner_won && g_race_serial) {
        g_won_sent = true;
        g_out_won = true;
        g_out_prize = 0;
        g_won_serial = g_race_serial;
        Log("race: this player won");
    }
    if ((state == 5 || state == 6) && !GateOwns(race) && g_partner_won && !G<uint8_t>(local + 0x5D)) {
        G<uint8_t>(local + 0x5D) = 1;  // let this copy's normal finish report the shared victory
        Log("race: the partner won, this career counts it as won");
    }
    uint32_t rewards = P(P(kCareer) + 0x14), data = P(P(kCareer) + 0x0C);
    if (state < 7 || state > 10 || !rewards || !data || !G<uint8_t>(race + 0x0E) ||
        !Mapped(rewards + 0xBC, 4) || !Mapped(data + 0xACC, 4)) return;
    int32_t paid = int32_t(R32(rewards + 0xBC));
    if (paid < 0) return;
    if (g_won_sent && !g_prize_sent) {
        g_prize_sent = true;
        g_out_prize = paid;
        g_won_serial = g_race_serial;
        g_out_won = true;
        Log("race: won %d", paid);
    }
    if (g_partner_won && g_partner_prize > paid) {
        int32_t money = int32_t(R32(data + 0xACC));
        int64_t topped = int64_t(money) + int64_t(g_partner_prize) - paid;
        if (money < 0 || topped > std::numeric_limits<int32_t>::max()) return;
        W32(data + 0xACC, uint32_t(topped));
        W32(rewards + 0xBC, uint32_t(g_partner_prize));  // UI and repeat packets see the amount already paid
        G<uint8_t>(P(kCareer) + 0x20) = 1;
        Log("race: prize topped up by %d to the winner's %d", g_partner_prize - paid, g_partner_prize);
    }
}

}  // namespace

void Tick(bool hosting, bool connected) {
    std::lock_guard lock(g_lock);
    uint64_t now = GetTickCount64();
    g_challenge_input = connected && (now < g_lights_until || g_phase != 0);
    if (!connected) {
        g_have_start = false;
        if (g_phase) {
            uint32_t rec = FindRecord(P(P(kCareer) + 0x0C), int(g_record_id), 0, 0);
            if (rec) G<uint8_t>(rec + 0x09) = g_saved_hookman, G<uint8_t>(rec + 0x0A) = g_saved_side;
            g_phase = 0;
        }
        // Keep a received result only for this exact loaded race, not a later solo race/world.
        if (g_bound_race && !Cruise() && P(P(kRaceHolder) + 4) == g_bound_race) RaceTick(false);
        else g_partner_won = false, g_partner_prize = 0, g_race_serial = 0, g_bound_race = 0;
        return;
    }
    if (now < g_lights_until) Headlights();
    if (hosting) { HostTick(); CaptureHostSetup(); }
    else { PartnerTick(now); ApplyHostSetup(); }
    if (!Cruise() && g_race_serial) g_bound_race = P(P(kRaceHolder) + 4);
    else if (Cruise()) g_bound_race = 0;
    RaceTick(true);
    g_challenge_input = now < g_lights_until || g_phase != 0;
}

bool ChallengeInput() {
    std::lock_guard lock(g_lock);
    return g_challenge_input;
}

void OnStart(const Start& s) {
    std::lock_guard lock(g_lock);
    if (!s.serial || s.city < 0 || s.city > 3 || s.race_idx < 0 || s.race_idx >= 160) return;
    if (g_have_start && int32_t(s.serial - g_start_in.serial) <= 0) return;
    BeginRace(s.serial);
    ai::SetRace(s.serial, s.city, s.race_idx, true);
    g_start_in = s;
    g_start_in.name[sizeof s.name - 1] = 0;
    g_have_start = true;
    Log("race: the host starts %s", g_start_in.name);
}

void OnPartnerWon(uint32_t serial, int32_t prize) {
    std::lock_guard lock(g_lock);
    if (!serial || serial != g_race_serial || prize < 0) return;  // stale/other race, never credit it here
    if (g_gate_resolved && !g_gate_shared_won) return; // a timeout/forfeit decision is final for this serial
    if (!g_partner_won) Log("race: the partner says it won");
    g_partner_won = true;
    if (prize > g_partner_prize) g_partner_prize = prize;
}

void OnPartnerFinished(const Finished& finished) {
    std::lock_guard lock(g_lock);
    if (!ValidFinished(finished) || finished.serial != g_race_serial || g_peer_finished) return;
    g_finished_peer = finished;
    g_peer_finished = true;
    // A timeout's local credit decision stays final, but the real peer still belongs in native ranking.
    // Late metadata must not rearm a completed race or revive its shared prize/victory.
    if (finished.won && !(g_gate_resolved && !g_gate_shared_won)) g_partner_won = true;
    Log("race: peer terminal serial %u won=%u dnf=%u", finished.serial, finished.won, finished.dnf);
}

bool TakeFinished(Finished& out) {
    std::lock_guard lock(g_lock);
    if (!g_local_finished || !g_race_serial || g_finished_out.serial != g_race_serial) return false;
    uint64_t now = GetTickCount64();
    if (g_finished_send_at && now - g_finished_send_at < 500) return false;
    g_finished_send_at = now ? now : 1;
    out = g_finished_out;
    return true;
}

bool TakeStart(Start& out) {
    std::lock_guard lock(g_lock);
    if (!g_out_start) return false;
    g_out_start = false;
    out = g_start_out;
    return true;
}

bool TakeSetup(Setup& out) {
    std::lock_guard lock(g_lock);
    if (!g_setup_out.serial || g_setup_out.serial != g_race_serial) return false;
    uint64_t now = GetTickCount64();
    if (g_setup_send_at && now - g_setup_send_at < 100) return false;
    g_setup_send_at = now ? now : 1;
    out = g_setup_out;
    return true;
}

void OnSetup(const Setup& setup) {
    std::lock_guard lock(g_lock);
    if (!g_have_start || setup.serial != g_race_serial || !setup.revision || setup.reserved ||
        setup.city != g_start_in.city || setup.race_idx != g_start_in.race_idx || setup.opponent_limit > 100 ||
        (g_have_setup && int32_t(setup.revision - g_setup_in.revision) <= 0)) return;
    for (const auto& opponent : setup.opponents) {
        int32_t model;
        std::memcpy(&model, opponent, 4);
        if (model < -1 || model >= 127 || opponent[4] > 1 || opponent[0x27]) return;
    }
    g_setup_in = setup;
    g_have_setup = true;
}

bool TakeWon(uint32_t& serial, int32_t& prize) {
    std::lock_guard lock(g_lock);
    if (!g_out_won) return false;
    g_out_won = false;
    serial = g_won_serial;
    prize = g_out_prize;
    return true;
}

std::string GoToOpenRace(int index) {
    std::lock_guard lock(g_lock);
    uint32_t data = P(P(kCareer) + 0x0C), cruise = Cruise();
    if (!data || !cruise) return "error not in free roam";
    uint32_t begin = P(data + 0xA5C), end = P(data + 0xA60);
    std::string list;
    char line[96];
    int n = 0;
    for (uint32_t r = begin; r && r + kRecord <= end && Mapped(r, kRecord); r += kRecord, n++) {
        int city = G<int8_t>(r + 0x08), idx = G<int16_t>(r + 0x06);
        std::snprintf(line, sizeof line, "%d: id %u city %d race %d %s\n", n, G<uint16_t>(r + 0x04), city, idx, RaceName(city, idx));
        list += line;
        if (n != index) continue;
        uint32_t desc = P(P(kRaceDb) + uint32_t(city) * 0x50 + 0x20) + uint32_t(idx) * 0x30;
        car::Pose at;
        if (!Ptr(desc + 0x1C) || !car::Local(at)) return "error no start position";
        // from outside the marker (35 m around: it arms), then into it, the lights on for a while
        at.m[3][0] = RF(desc + 0x14) + 60.0f, at.m[3][1] = RF(desc + 0x18) + 0.5f, at.m[3][2] = RF(desc + 0x1C);
        car::Place(at);
        Sleep(400);
        at.m[3][0] -= 60.0f;
        car::Place(at);
        g_lights_until = GetTickCount64() + 6000;
        return "ok going to " + std::string(RaceName(city, idx)) + "\n" + list;
    }
    return list.empty() ? "no open races" : list;
}

std::string Describe() {
    std::lock_guard lock(g_lock);
    char s[512];
    std::snprintf(s, sizeof s, "race_state=%d cruise=%d phase=%d start_in=%s partner_won=%d won_sent=%d prize=%d gate=%d gate_reason=%s terminal=%d/%d",
                  RaceState(), Cruise() != 0, g_phase, g_have_start ? g_start_in.name : "-", g_partner_won,
                  g_won_sent, g_partner_prize, int(g_gate), g_gate_reason, g_local_finished, g_peer_finished);
    return s;
}

}  // namespace mod::race
