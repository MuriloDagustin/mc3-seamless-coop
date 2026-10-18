// The career race flow shared by the copies (race.h, docs/RE-CORRIDA.txt).
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "car.h"
#include "mod.h"
#include "race.h"

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
uint64_t g_phase_at = 0;
// both
uint64_t g_lights_until = 0;  // the test's challenge: the lights kept on until then
bool g_partner_won = false, g_out_won = false, g_won_sent = false, g_partner_gone = false, g_forfeited = false;
uint32_t g_won_serial = 0, g_race_serial = 0;
// the prize: the winner sends what it was paid (1st place), a copy credited with the partner's win tops its own up
int32_t g_partner_prize = 0, g_out_prize = 0;
bool g_prize_sent = false, g_topped = false;
std::string g_last;

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
    g_race_serial = s.serial;
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

// a win for both, and a partner gone, in the race now running here
void RaceTick() {
    uint32_t race = P(P(kRaceHolder) + 4);
    int state = RaceState();
    if (!race || Cruise() || !Mapped(race + kEntry, 3 * kEntrySize)) return;
    uint32_t rewards = P(P(kCareer) + 0x14), data = P(P(kCareer) + 0x0C);
    if (state >= 8 && state <= 10 && rewards && data && G<uint8_t>(race + 0x0E)) {  // paid (race+0x0E), not saved yet
        int32_t paid = int32_t(R32(rewards + 0xBC));
        if (g_won_sent && !g_prize_sent) {  // this player won: what 1st place pays
            g_prize_sent = true;
            g_out_prize = paid;
            g_out_won = true;
            Log("race: won %d", paid);
        }
        if (g_partner_won && g_partner_prize > paid && !g_topped) {
            g_topped = true;
            W32(data + 0xACC, R32(data + 0xACC) + uint32_t(g_partner_prize - paid));
            G<uint8_t>(P(kCareer) + 0x20) = 1;  // the career is saved by the autosave that follows
            Log("race: prize topped up by %d to the winner's %d", g_partner_prize - paid, g_partner_prize);
        }
    }
    if (state != 5 && state != 6) {
        if (state < 5) g_won_sent = false, g_forfeited = false, g_prize_sent = false, g_topped = false, g_partner_prize = 0;
        return;
    }
    if (P(P(kGameState) + 4) == 3) return;  // that state skips the race end: nothing written now
    uint32_t local = race + kEntry, partner = race + kEntry + kEntrySize;
    if (G<uint8_t>(local + 0x04) && G<uint8_t>(local + 0x5D) && !g_won_sent) {  // finished first in this world
        g_won_sent = true;
        g_out_won = true;
        g_won_serial = g_race_serial;
        Log("race: this player won");
    }
    if (g_partner_won && !G<uint8_t>(local + 0x5D)) {  // before the last human finishes here: ReportRaceResult(0)
        G<uint8_t>(local + 0x5D) = 1;
        Log("race: the partner won, this career counts it as won");
    }
    if (g_partner_gone && !g_forfeited && !G<uint8_t>(partner + 0x04)) {
        g_forfeited = true;
        if (!G<uint8_t>(local + 0x04)) {  // the local player still racing: the game's forfeit by hand (0x1CC710)
            G<uint8_t>(partner + 0x06) = 1, G<uint8_t>(partner + 0x5D) = 0;
            G<float>(partner + 0x0C) = 0.0f;
            W32(partner + 0x18, 0);
            G<uint8_t>(partner + 0x04) = 1;
            W32(race + 0x1250, R32(race + 0x1250) + 1);
        } else {  // already finished: the game's own time-limit forfeit ends the race
            G<float>(partner + 0x0C) = 0.0f;
            G<uint8_t>(race + 0x6F4) = 1;
        }
        Log("race: the partner left, forfeited here");
    }
}

}  // namespace

void Tick(bool hosting, bool connected) {
    std::lock_guard lock(g_lock);
    uint64_t now = GetTickCount64();
    if (!connected) {
        g_have_start = false, g_phase = 0, g_partner_won = false, g_partner_gone = false;
        return;
    }
    if (now < g_lights_until) Headlights();
    if (hosting) HostTick();
    else PartnerTick(now);
    if (Cruise()) g_partner_won = false, g_partner_gone = false, g_partner_prize = 0;
    RaceTick();
}

void OnStart(const Start& s) {
    std::lock_guard lock(g_lock);
    if (g_have_start && s.serial == g_start_in.serial) return;
    g_start_in = s;
    g_start_in.name[sizeof s.name - 1] = 0;
    g_have_start = true;
    Log("race: the host starts %s", g_start_in.name);
}

void OnPartnerWon(uint32_t, int32_t prize) {
    std::lock_guard lock(g_lock);
    if (!g_partner_won) Log("race: the partner says it won");
    g_partner_won = true;
    if (prize > g_partner_prize) g_partner_prize = prize;
}

void OnPartnerGone() {
    std::lock_guard lock(g_lock);
    g_partner_gone = true;
}

bool TakeStart(Start& out) {
    std::lock_guard lock(g_lock);
    if (!g_out_start) return false;
    g_out_start = false;
    out = g_start_out;
    return true;
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
    char s[256];
    std::snprintf(s, sizeof s, "race_state=%d cruise=%d phase=%d start_in=%s partner_won=%d partner_gone=%d won_sent=%d",
                  RaceState(), Cruise() != 0, g_phase, g_have_start ? g_start_in.name : "-", g_partner_won,
                  g_partner_gone, g_won_sent);
    return s;
}

}  // namespace mod::race
