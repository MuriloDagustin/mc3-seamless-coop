// Co-op sessions, after the Black co-op's (C:\dev\black-coop src/game/coop.cpp): a star of up to eight copies,
// the host in slot 0 routing between the others, over one UDP socket or EOS P2P (coop_online.cpp).
// Each copy sends its own car 30 times a second, latest wins; the copies show each other's cars (car.cpp).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "car.h"
#include "core/log.h"
#include "mod.h"
#include "net/coop_net.h"
#include "net/coop_online.h"
#include "net/session.h"
#include "race.h"
#include "traffic.h"
#include "settings.h"
#include "ai.h"

// the game identity copies must share (the XBE's title, version and size, from the guest image), 40 characters
extern char kImageSha1[41];

namespace coop {

namespace {

constexpr uint64_t kSendMs = 33;  // 30 a second whatever the frame rate
constexpr uint64_t kHelloMs = 500;
constexpr uint64_t kTimeoutMs = 5000;

constexpr uint32_t kMagic = 0x4F43334D;  // "M3CO"
constexpr uint16_t kProtocol = 6; // pose world identity; all copies must run the same build
enum Type : uint8_t { kHello = 1, kWelcome, kState, kBye, kDiscover, kOffer, kRoster, kFull, kCar, kRaceStart, kWon, kTraffic, kFinished, kAi, kRaceSetup };

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint16_t protocol;
    uint8_t type, flags;  // flags: destination << 4 | source
    uint32_t session;     // drawn at each start: a partner that started again counts its seq from 0
    uint32_t seq, ms;
};
struct Hello {
    Header h;
    char xbe_sha1[40];
};
struct Roster {
    Header h;
    uint32_t sessions[kMaxPlayers];
    uint8_t mask;
};
struct Offer {
    Hello hello;
    char name[64];
    uint8_t busy;
};
struct State {
    Header h;
    char nickname[kNicknameBytes];
    uint8_t in_play;  // driving in the city
    int8_t city;
    uint8_t pad[2];
    mod::race::World world;
    mod::car::Pose pose;
};
struct CarPacket {  // the sender's own campaign car, the game's CarCfg as it is
    Header h;
    uint8_t cfg[mod::car::kCarCfgBytes];
};
struct RacePacket {  // the host started a career race
    Header h;
    mod::race::Start start;
};
struct WonPacket {  // the sender won the race it shared (again with what 1st place paid it)
    Header h;
    uint32_t serial;
    int32_t prize;
};
struct FinishedPacket { Header h; mod::race::Finished result; };
struct SetupPacket { Header h; mod::race::Setup setup; };
#pragma pack(pop)
constexpr int kMaxDatagram = 1170;  // also fits the EOS P2P transport
union Packet {
    Packet() : raw{} {}
    Header h;
    Hello hello;
    State state;
    uint8_t raw[1500];
};

struct Config {
    Role role = Role::Off;
    std::string host;
    uint16_t port = kDefaultPort;
    bool online = false;
} g_cfg;

std::atomic<bool> g_net{false};
std::atomic<bool> g_command{false}, g_searching{false}, g_rescue{false};
std::mutex g_ui_mutex;
Session g_ui;
std::string g_nickname = "Player", g_pending_nickname;
enum class Command { None, Start, Stop, Search };
struct Request {
    Command command = Command::None;
    Role role = Role::Off;
    std::string host;
    uint16_t port = kDefaultPort;
    bool online = false;
} g_request;
uint64_t g_search_until = 0, g_search_tx = 0;
uint16_t g_search_port = kDefaultPort;
uint32_t g_session = 0, g_seq = 0;
int g_local_player = 15;  // 15 until the host gives a slot
uint64_t g_roster_at = 0;
std::string g_reject;

struct Participant {
    std::string nickname = "Player";
    bool have = false, welcomed = false;
    net::Peer endpoint{};
    uint32_t session = 0, rx_seq = 0;
    uint64_t last_rx = 0, last_tx = 0, last_hello = 0, state_at = 0, car_tx = 0;
    State remote{};
};
std::array<Participant, kMaxPlayers> g_players;

// the race events, sent a few times each (UDP loses some): the receivers know them by their serials
struct Repeat {
    uint8_t raw[1200];
    int bytes = 0, left = 0;
    uint64_t next = 0;
};
std::array<Repeat, 4> g_repeats;
void Queue(const void* data, int bytes) {
    for (auto& r : g_repeats)
        if (!r.left) {
            std::memcpy(r.raw, data, size_t(bytes));
            r.bytes = bytes, r.left = 6, r.next = 0;
            return;
        }
}

// the race-setup slot a partner's car takes in this copy's world: the local player is slot 0, the others 1.. in
// player order
int SlotOf(int player) {
    int slot = 1;
    for (int i = 0; i < player; i++)
        if (i != g_local_player && g_players[i].have) slot++;
    return slot;
}

uint64_t NowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

Header Make(Type t, int to, uint64_t now) {
    return {kMagic, kProtocol, t, uint8_t((to << 4) | (g_local_player & 15)), g_session, ++g_seq, uint32_t(now)};
}

bool SendTo(int player, const void* data, int bytes) {
    if (player < 0 || player >= kMaxPlayers || player == g_local_player || !g_players[player].have) return false;
    // only the host holds a connection to everyone: clients reach each other through it
    const auto& endpoint = g_cfg.role == Role::Host ? g_players[player].endpoint : g_players[0].endpoint;
    return net::Send(endpoint, data, bytes);
}

void SendHello(int to, uint64_t now) {
    Hello hello{Make(kHello, to, now), {}};
    std::memcpy(hello.xbe_sha1, kImageSha1, 40);
    SendTo(to, &hello, sizeof hello);
}

void SendRoster(uint64_t now) {
    if (g_cfg.role != Role::Host) return;
    Roster r{};
    r.sessions[0] = g_session;
    r.mask = 1;
    for (int i = 1; i < kMaxPlayers; i++)
        if (g_players[i].have) {
            r.mask |= uint8_t(1u << i);
            r.sessions[i] = g_players[i].session;
        }
    for (int i = 1; i < kMaxPlayers; i++)
        if (g_players[i].have) {
            r.h = Make(kRoster, i, now);
            SendTo(i, &r, sizeof r);
        }
}

bool Finite(const State& s) {
    const float* f = &s.pose.m[0][0];
    for (size_t i = 0; i < sizeof s.pose / sizeof(float); i++)
        if (!std::isfinite(f[i])) return false;
    return true;
}

void Sample(State& s, int to, uint64_t now) {
    std::memset(&s, 0, sizeof s);
    s.h = Make(kState, to, now);
    std::snprintf(s.nickname, sizeof s.nickname, "%s", g_nickname.c_str());
    s.city = int8_t(mod::car::City());
    s.world = mod::race::Context();
    s.in_play = mod::race::ValidWorld(s.world) && mod::car::InPlay() && mod::car::Local(s.pose) ? 1 : 0;
    if (!Finite(s)) s.in_play = 0, s.pose = {};
}

void Forget(int player) {
    mod::car::Remove(SlotOf(player));
    g_players[player] = {};
}

void Receive(uint64_t now) {
    Packet in;
    net::Peer from;
    for (int i = 0, n; i < 256 && (n = net::Receive(from, &in, sizeof in)) >= 0; i++) {
        if (n < int(sizeof(Header)) || in.h.magic != kMagic || in.h.protocol != kProtocol) continue;
        if (in.h.type == kDiscover) {
            if (!g_net || g_cfg.role != Role::Host || n != sizeof(Hello) || std::memcmp(in.hello.xbe_sha1, kImageSha1, 40))
                continue;
            Offer offer{};
            offer.hello.h = Make(kOffer, 15, now);
            std::memcpy(offer.hello.xbe_sha1, kImageSha1, 40);
            std::snprintf(offer.name, sizeof offer.name, "%s", net::HostName().c_str());
            offer.busy = PlayerCount() >= kMaxPlayers;
            net::Send(from, &offer, sizeof offer);
            continue;
        }
        if (in.h.type == kOffer) {
            if (!g_searching || n != sizeof(Offer) || std::memcmp(in.hello.xbe_sha1, kImageSha1, 40)) continue;
            Offer offer;
            std::memcpy(&offer, in.raw, sizeof offer);
            offer.name[sizeof offer.name - 1] = 0;
            for (char& c : offer.name)
                if (c && (uint8_t(c) < 32 || uint8_t(c) > 126)) c = '?';
            std::string address = net::Text(from);
            address.resize(address.find(':'));
            std::lock_guard lock(g_ui_mutex);
            auto it = std::find_if(g_ui.servers.begin(), g_ui.servers.end(), [&](const Server& s) {
                return s.session == offer.hello.h.session && s.port == from.port;
            });
            Server server{address, offer.name, from.port, offer.busy != 0, offer.hello.h.session};
            // one host answers through each of its adapters: one row a session, loopback first for copies on this PC
            if (it != g_ui.servers.end()) {
                if (it->address == "127.0.0.1") server.address = it->address;
                *it = server;
            } else if (g_ui.servers.size() < 32) {
                g_ui.servers.push_back(server);
            }
            continue;
        }
        if (!g_net) continue;
        int source = in.h.flags & 15, destination = in.h.flags >> 4;
        if (g_cfg.role == Role::Host) {
            int slot = -1;
            for (int k = 1; k < kMaxPlayers; k++)
                if (g_players[k].have && g_players[k].endpoint == from) {
                    slot = k;
                    break;
                }
            if (slot < 0) {
                if (in.h.type != kHello || n != int(sizeof(Hello)) || destination != 0 ||
                    std::memcmp(in.hello.xbe_sha1, kImageSha1, 40))
                    continue;
                for (int k = 1; k < kMaxPlayers; k++)
                    if (!g_players[k].have) {
                        slot = k;
                        break;
                    }
                if (slot < 0) {
                    Header full = Make(kFull, 15, now);
                    full.flags = 0xF0;
                    net::Send(from, &full, sizeof full);
                    continue;
                }
                g_players[slot] = {};
                g_players[slot].endpoint = from;
                g_players[slot].have = true;
                LOGR("coop: player %d from %s", slot + 1, net::Text(from).c_str());
            }
            if (source != slot && !(in.h.type == kHello && source == 15)) continue;
            source = slot;
            if (destination != 0) {  // for another client: pass it on
                if (destination >= kMaxPlayers || destination == source || !g_players[destination].have ||
                    in.h.session != g_players[source].session || (in.h.type != kState && in.h.type != kCar && in.h.type != kWon && in.h.type != kFinished))
                    continue;
                g_players[source].last_rx = now;
                net::Send(g_players[destination].endpoint, &in, n);
                continue;
            }
        } else {
            if (!g_players[0].have || from != g_players[0].endpoint || source >= kMaxPlayers) continue;
            if (in.h.type == kWelcome && source == 0 && n == sizeof(Header) && destination > 0 && destination < kMaxPlayers)
                g_local_player = destination;
            if (in.h.type == kFull && source == 0) {
                g_reject = "This room already has eight players.";
                continue;
            }
            if (destination != g_local_player || source == g_local_player || !g_players[source].have) continue;
            if (source && in.h.session != g_players[source].session) continue;
            if (in.h.type == kRoster && source != 0) continue;
        }
        Participant& p = g_players[source];
        if (in.h.session != p.session) {  // the partner started again: a new sequence, nothing of the old one
            if (g_cfg.role == Role::Join && source == 0) mod::race::ResetNetwork();
            p.session = in.h.session;
            p.rx_seq = 0;
            p.remote = {};
            mod::car::Remove(SlotOf(source));
        }
        p.last_rx = now;
        switch (in.h.type) {
            case kHello: {
                Header w = Make(kWelcome, source, now);
                SendTo(source, &w, sizeof w);
                p.welcomed = true;
                g_roster_at = 0;
                break;
            }
            case kWelcome:
                if (!p.welcomed) LOGR("coop: joined %s as player %d", net::Text(p.endpoint).c_str(), g_local_player + 1);
                p.welcomed = true;
                break;
            case kRoster: {
                if (n != sizeof(Roster) || g_cfg.role != Role::Join || source != 0 || !p.welcomed) break;
                Roster roster;
                std::memcpy(&roster, &in, sizeof roster);
                if (!(roster.mask & 1) || !(roster.mask & (1u << g_local_player)) ||
                    roster.sessions[g_local_player] != g_session)
                    break;
                for (int k = 1; k < kMaxPlayers; k++) {
                    if (k == g_local_player) continue;
                    bool present = (roster.mask & (1u << k)) && roster.sessions[k];
                    if (g_players[k].have == present && (!present || g_players[k].session == roster.sessions[k])) continue;
                    Forget(k);
                    g_players[k].have = g_players[k].welcomed = present;
                    g_players[k].session = roster.sessions[k];
                    g_players[k].last_rx = now;
                }
                break;
            }
            case kState:
                if (n == int(sizeof(State)) && Finite(in.state) && int32_t(in.h.seq - p.rx_seq) > 0) {
                    p.remote = in.state;
                    p.nickname = CleanNickname(std::string(
                        in.state.nickname, std::find(std::begin(in.state.nickname), std::end(in.state.nickname), '\0')));
                    p.state_at = now;
                    p.rx_seq = in.h.seq;
                    if (in.state.in_play && in.state.city == in.state.world.city &&
                        mod::race::MatchesWorld(in.state.world, mod::race::Context()))
                        mod::car::DriveWorld(SlotOf(source), in.state.pose, in.h.ms, in.state.world);
                    else mod::car::DriveWorld(SlotOf(source), {}, in.h.ms, {}); // invalidate old interpolation too
                }
                break;
            case kCar:
                if (n == int(sizeof(CarPacket))) {
                    CarPacket car;
                    std::memcpy(&car, &in, sizeof car);
                    mod::car::OnPartnerCar(SlotOf(source), car.cfg, in.h.seq);
                }
                break;
            case kRaceStart:
                if (n == int(sizeof(RacePacket)) && source == 0) {
                    RacePacket rp;
                    std::memcpy(&rp, &in, sizeof rp);
                    mod::race::OnStart(rp.start);
                }
                break;
            case kWon:
                if (n == int(sizeof(WonPacket))) {
                    WonPacket wp;
                    std::memcpy(&wp, &in, sizeof wp);
                    mod::race::OnPartnerWon(wp.serial, wp.prize);
                }
                break;
            case kFinished:
                if (n == int(sizeof(FinishedPacket))) {
                    FinishedPacket fp;
                    std::memcpy(&fp, &in, sizeof fp);
                    mod::race::OnPartnerFinished(fp.result);
                }
                break;
            case kAi:
                if (source == 0 && g_cfg.role == Role::Join) mod::ai::Receive(in.raw + sizeof(Header), n - int(sizeof(Header)));
                break;
            case kRaceSetup:
                if (source == 0 && g_cfg.role == Role::Join && n == int(sizeof(SetupPacket))) {
                    SetupPacket sp;
                    std::memcpy(&sp, &in, sizeof sp);
                    mod::race::OnSetup(sp.setup);
                }
                break;
            case kTraffic:
                if (source == 0 && g_cfg.role == Role::Join)
                    mod::traffic::Receive(in.raw + sizeof(Header), n - int(sizeof(Header)));
                break;
            case kBye:
                LOGR("coop: player %d left", source + 1);
                if (g_cfg.role == Role::Host) Forget(source);
                else p.remote = {}, p.welcomed = source != 0 ? p.welcomed : false, mod::car::Remove(source);
                break;
        }
    }
}

void Send(uint64_t now) {
    mod::race::Start start;
    if (g_cfg.role == Role::Host && mod::race::TakeStart(start)) {
        RacePacket rp{};
        rp.h.type = kRaceStart;
        rp.start = start;
        Queue(&rp, sizeof rp);
    }
    mod::race::Setup setup;
    if (g_cfg.role == Role::Host && mod::race::TakeSetup(setup)) {
        SetupPacket sp{};
        sp.setup = setup;
        for (int i = 1; i < kMaxPlayers; i++) if (g_players[i].have && g_players[i].welcomed) {
            sp.h = Make(kRaceSetup, i, now);
            SendTo(i, &sp, sizeof sp);
        }
    }
    uint32_t won;
    int32_t prize;
    if (mod::race::TakeWon(won, prize)) {
        WonPacket wp{};
        wp.h.type = kWon;
        wp.serial = won;
        wp.prize = prize;
        Queue(&wp, sizeof wp);
    }
    // Periodic state, independent of the finite event queue: a lost first terminal packet cannot deadlock a gate.
    mod::race::Finished finished;
    if (mod::race::TakeFinished(finished)) {
        FinishedPacket fp{};
        fp.result = finished;
        for (int i = 0; i < kMaxPlayers; i++)
            if (i != g_local_player && g_players[i].have && g_players[i].welcomed) {
                fp.h = Make(kFinished, i, now);
                SendTo(i, &fp, sizeof fp);
            }
    }
    for (auto& r : g_repeats) {
        if (!r.left || now < r.next) continue;
        r.left--, r.next = now + 500;
        auto* h = reinterpret_cast<Header*>(r.raw);
        for (int i = 0; i < kMaxPlayers; i++) {
            if (i == g_local_player || !g_players[i].have) continue;
            Type type = Type(h->type);
            *h = Make(type, i, now);
            SendTo(i, r.raw, r.bytes);
        }
    }
    static std::vector<std::vector<uint8_t>> traffic;
    if (g_cfg.role == Role::Host && PlayerCount() > 1 && mod::traffic::Snapshot(traffic, now)) {
        Packet out;
        for (auto& chunk : traffic) {
            if (sizeof(Header) + chunk.size() > sizeof out.raw) continue;
            std::memcpy(out.raw + sizeof(Header), chunk.data(), chunk.size());
            for (int i = 1; i < kMaxPlayers; i++)
                if (g_players[i].have && g_players[i].welcomed) {
                    out.h = Make(kTraffic, i, now);
                    SendTo(i, out.raw, int(sizeof(Header) + chunk.size()));
                }
        }
    }
    static std::vector<std::vector<uint8_t>> opponents;
    if (g_cfg.role == Role::Host && PlayerCount() > 1 && mod::ai::Snapshot(opponents, now)) {
        Packet out;
        for (auto& one : opponents) {
            if (sizeof(Header) + one.size() > sizeof out.raw) continue;
            std::memcpy(out.raw + sizeof(Header), one.data(), one.size());
            for (int i = 1; i < kMaxPlayers; i++)
                if (g_players[i].have && g_players[i].welcomed) {
                    out.h = Make(kAi, i, now);
                    SendTo(i, out.raw, int(sizeof(Header) + one.size()));
                }
        }
    }
    if (g_cfg.role == Role::Host) {
        for (int i = 1; i < kMaxPlayers; i++)
            if (g_players[i].have && now - g_players[i].last_rx > kTimeoutMs) {
                LOGR("coop: player %d timed out", i + 1);
                Forget(i);
                g_roster_at = 0;
            }
        if (!g_roster_at || now - g_roster_at >= 500) {
            SendRoster(now);
            g_roster_at = now;
        }
    }
    for (int i = 0; i < kMaxPlayers; i++) {
        Participant& p = g_players[i];
        if (i == g_local_player || !p.have) continue;
        if (g_cfg.role == Role::Join && i == 0 && (!p.welcomed || now - p.last_rx > kTimeoutMs)) {
            if (now - p.last_hello >= kHelloMs) {
                p.welcomed = false;
                SendHello(0, now);
                p.last_hello = now;
            }
            continue;
        }
        if (!p.car_tx || now - p.car_tx >= 1000) {  // the campaign car, again now and then: a lost one comes again
            CarPacket car{};
            if (mod::car::LocalCarCfg(car.cfg)) {
                car.h = Make(kCar, i, now);
                SendTo(i, &car, sizeof car);
            }
            p.car_tx = now;
        }
        if (now - p.last_tx < kSendMs) continue;
        p.last_tx = now;
        State s;
        Sample(s, i, now);
        SendTo(i, &s, sizeof s);
    }
}

void PublishSession(const std::string& error = {}) {
    std::lock_guard lock(g_ui_mutex);
    g_ui.nickname = g_nickname;
    g_ui.role = g_cfg.role;
    g_ui.running = g_net;
    g_ui.online = g_net && g_cfg.online;
    g_ui.online_available = online::Built();
    g_ui.room_code = g_ui.online ? online::Code() : std::string{};
    g_ui.players = g_net ? PlayerCount() : 0;
    g_ui.members.clear();
    if (g_net)
        for (int i = 0; i < kMaxPlayers; i++) {
            if (i == g_local_player) g_ui.members.push_back({i, g_nickname, true, i == 0});
            else if (Connected(i)) g_ui.members.push_back({i, g_players[i].nickname, false, i == 0});
        }
    g_ui.port = g_cfg.port;
    g_ui.status = Status();
    if (!error.empty()) g_ui.error = error;
}

void StartSession() {
    if (g_cfg.role == Role::Off) return;
    g_local_player = g_cfg.role == Role::Host ? 0 : 15;
    if (g_cfg.online) {
        if (!online::Start(g_cfg.role == Role::Host, g_cfg.host, "mc3-coop-" + std::to_string(kProtocol) + "-" + kImageSha1)) {
            PublishSession(online::Error());
            return;
        }
    } else if (!net::Open(g_cfg.role == Role::Host ? g_cfg.port : 0)) {
        PublishSession("Could not open UDP port " + std::to_string(g_cfg.port) + ". Check whether it is already in use.");
        return;
    }
    if (g_cfg.role == Role::Join && !g_cfg.online) {
        g_players[0].endpoint = {net::Resolve(g_cfg.host), g_cfg.port};
        if (!g_players[0].endpoint.addr) {
            net::Close();
            PublishSession("Host address not found: " + g_cfg.host);
            return;
        }
        g_players[0].have = true;
    }
    mod::race::ResetNetwork();
    g_net = true;
    if (g_cfg.online) LOGR("coop: starting online %s", g_cfg.role == Role::Host ? "host" : "join");
    else if (g_cfg.role == Role::Host) LOGR("coop: host on UDP %u", g_cfg.port);
    else LOGR("coop: joining %s", net::Text(g_players[0].endpoint).c_str());
    PublishSession();
}

void StopSession() {
    mod::traffic::Stop();
    mod::ai::Stop();
    if (g_net)
        for (int i = 0; i < kMaxPlayers; i++)
            if (i != g_local_player && g_players[i].have) {
                Header bye = Make(kBye, i, NowMs());
                SendTo(i, &bye, sizeof bye);
            }
    for (int i = 0; i < kMaxPlayers; i++) mod::car::Remove(i);
    net::Close();
    if (g_net) LOGR("coop: session stopped");
    g_net = false;
    g_searching = false;
    g_cfg.role = Role::Off;
    g_cfg.online = false;
    g_players = {};
    g_local_player = 15;
    g_roster_at = 0;
    g_rescue = false;
}

// the nearest partner in play in this city
const Participant* NearestPartner(int& player) {
    mod::car::Pose me;
    bool have_me = mod::car::Local(me);
    const Participant* best = nullptr;
    float best_d = 0;
    for (int i = 0; i < kMaxPlayers; i++) {
        const Participant& p = g_players[i];
        if (!Connected(i) || !p.remote.in_play || p.remote.city != mod::car::City() ||
            !mod::race::MatchesWorld(p.remote.world, mod::race::Context())) continue;
        float d = 0;
        if (have_me)
            for (int k = 0; k < 3; k++) d += (p.remote.pose.m[3][k] - me.m[3][k]) * (p.remote.pose.m[3][k] - me.m[3][k]);
        if (!best || d < best_d) best = &p, best_d = d, player = i;
    }
    return best;
}

// "take me to my partner": a few car lengths behind the partner's car, on its heading
void Rescue() {
    int player = -1;
    const Participant* p = NearestPartner(player);
    if (!p || !mod::car::InPlay()) {
        LOGR("coop: rescue asked, no partner in play in this city");
        return;
    }
    mod::car::Pose at = p->remote.pose;
    for (int k = 0; k < 3; k++) at.m[3][k] -= at.m[2][k] * 8.0f;
    at.m[3][1] += 1.0f;
    mod::car::Place(at);
    LOGR("coop: placed by player %d", player + 1);
}

}  // namespace

int LocalPlayer() { return g_local_player; }

std::string CleanNickname(const std::string& name) {
    // UTF-8 kept whole, at most kNicknameCharacters characters, printable only
    wchar_t wide[kNicknameBytes]{};
    if (name.empty() || name.size() >= kNicknameBytes) return "Player";
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), int(name.size()), wide, kNicknameBytes);
    std::wstring clean;
    for (int i = 0; i < count && clean.size() < kNicknameCharacters; i++) {
        wchar_t c = wide[i];
        if (c >= 32 && !(c >= 127 && c <= 159) && !(c >= 0xD800 && c <= 0xDFFF) && c != 0x2028 && c != 0x2029) clean += c;
    }
    auto first = clean.find_first_not_of(L' '), last = clean.find_last_not_of(L' ');
    if (first == std::wstring::npos) return "Player";
    clean = clean.substr(first, last - first + 1);
    char utf8[kNicknameBytes]{};
    int bytes = WideCharToMultiByte(CP_UTF8, 0, clean.data(), int(clean.size()), utf8, kNicknameBytes - 1, nullptr, nullptr);
    return bytes ? std::string(utf8, bytes) : "Player";
}

void RequestNickname(const std::string& name) {
    std::string clean = CleanNickname(name);
    mod::Settings().coop_nickname = clean;
    mod::SaveSettings();
    std::lock_guard lock(g_ui_mutex);
    g_ui.nickname = clean;
    g_pending_nickname = clean;
}

bool Hosting() { return g_net && g_cfg.role == Role::Host; }
bool Networked() { return g_net; }
bool Connected(int player) {
    return g_net && player >= 0 && player < kMaxPlayers && player != g_local_player && g_players[player].have;
}
int PlayerCount() {
    int count = 1;
    for (int i = 0; i < kMaxPlayers; i++)
        if (i != g_local_player && g_players[i].have) count++;
    return count;
}

std::string Players() {
    std::string out = "players=" + std::to_string(PlayerCount()) + "/8 local=" + std::to_string(g_local_player + 1) +
                      " nick=\"" + g_nickname + "\"\n";
    char line[256];
    uint64_t now = NowMs();
    for (int i = 0; i < kMaxPlayers; i++)
        if (Connected(i)) {
            const Participant& p = g_players[i];
            std::snprintf(line, sizeof line, "P%d nick=\"%s\" heard=%llums in_play=%u city=%d pos=%.2f,%.2f,%.2f\n", i + 1,
                          p.nickname.c_str(), (unsigned long long)(p.last_rx ? now - p.last_rx : 0), p.remote.in_play,
                          int(p.remote.city), double(p.remote.pose.m[3][0]), double(p.remote.pose.m[3][1]),
                          double(p.remote.pose.m[3][2]));
            out += line;
        }
    return out;
}

bool PartnerCar(Remote& out) {
    int player = -1;
    const Participant* p = NearestPartner(player);
    if (!p) return false;
    out.player = player;
    out.nickname = p->nickname;
    std::memcpy(out.pos, p->remote.pose.m[3], sizeof out.pos);
    std::memcpy(out.vel, p->remote.pose.vel, sizeof out.vel);
    out.age_ms = NowMs() - p->state_at;
    out.in_play = p->remote.in_play;
    return true;
}

Session GetSession() {
    std::lock_guard lock(g_ui_mutex);
    Session s = g_ui;
    s.pending = g_command;
    s.searching = g_searching;
    return s;
}

void RequestStart(Role role, const std::string& host, uint16_t port) {
    std::lock_guard lock(g_ui_mutex);
    g_request = {Command::Start, role, host, port};
    g_command = true;
}
void RequestOnline(Role role, const std::string& code) {
    std::lock_guard lock(g_ui_mutex);
    g_request = {Command::Start, role, code, kDefaultPort, true};
    g_command = true;
}
void RequestStop() {
    std::lock_guard lock(g_ui_mutex);
    g_request.command = Command::Stop;
    g_command = true;
}
void RequestSearch(uint16_t port) {
    std::lock_guard lock(g_ui_mutex);
    g_request = {Command::Search, Role::Off, {}, port};
    g_command = true;
}
void RequestRescue() { g_rescue = true; }

void Init() {
    g_session = std::random_device{}() | 1;  // never 0, the value before any partner
    const mod::Options& o = mod::Settings();
    g_nickname = CleanNickname(o.coop_nickname);
    g_ui.nickname = g_nickname;
    // saved preferences are no request to connect at launch: F10, or an explicit --coop for this process
    g_cfg.role = Role::Off;
    g_cfg.port = uint16_t(o.coop_port && o.coop_port <= 65535 ? o.coop_port : kDefaultPort);
    g_cfg.host = o.join_ip;
    // per process (test copies on one PC share the settings file): MC3COOP_COOP=host | join:<host> | online:host |
    // online:<code>, MC3COOP_PORT, MC3COOP_NICK
    std::string v = mod::Env("MC3COOP_COOP");
    if (v == "host") g_cfg.role = Role::Host;
    else if (v.rfind("join:", 0) == 0) g_cfg.role = Role::Join, g_cfg.host = v.substr(5);
    else if (v.rfind("online:", 0) == 0) {
        g_cfg.online = true;
        g_cfg.role = v == "online:host" ? Role::Host : Role::Join;
        g_cfg.host = v == "online:host" ? std::string{} : v.substr(7);
    }
    v = mod::Env("MC3COOP_PORT");
    if (std::atoi(v.c_str()) > 0 && std::atoi(v.c_str()) <= 65535) g_cfg.port = uint16_t(std::atoi(v.c_str()));
    v = mod::Env("MC3COOP_NICK");
    if (!v.empty()) g_nickname = g_ui.nickname = CleanNickname(v);
    PublishSession();
}

void Frame() {
    uint64_t now = NowMs();
    {
        std::lock_guard lock(g_ui_mutex);
        if (!g_pending_nickname.empty()) {
            g_nickname = std::move(g_pending_nickname);
            g_pending_nickname.clear();
        }
    }
    static bool started = false;  // the command line's session, at the first frame
    if (!started) {
        started = true;
        StartSession();
    }
    if (g_command) {
        Request request;
        {
            std::lock_guard lock(g_ui_mutex);
            request = g_request;
            g_request = {};
            g_command = false;
            g_ui.error.clear();
        }
        if (request.command == Command::Start || request.command == Command::Stop) {
            StopSession();
            if (request.command == Command::Start && request.port &&
                (request.role == Role::Host || request.role == Role::Join)) {
                g_session = std::random_device{}() | 1;
                g_cfg.role = request.role;
                g_cfg.host = request.host;
                g_cfg.port = request.port;
                g_cfg.online = request.online;
                StartSession();
            }
        } else if (request.command == Command::Search) {
            if (!g_net && !net::Open(0)) {
                PublishSession("Could not open a socket for LAN search.");
            } else {
                g_search_port = request.port;
                g_search_until = now + 3000;
                g_search_tx = 0;
                g_searching = true;
                std::lock_guard lock(g_ui_mutex);
                g_ui.servers.clear();
            }
        }
    }
    online::Tick();
    if (g_net && g_cfg.online) {
        std::string error = online::Error();
        if (!error.empty()) {
            StopSession();
            PublishSession(error);
        } else if (g_cfg.role == Role::Join) {
            net::Peer peer = online::Partner();
            Participant& host = g_players[0];
            if (peer.online && (!host.have || peer != host.endpoint)) {
                host = {};
                host.endpoint = peer;
                host.have = true;
            } else if (!peer.online && host.have) {
                mod::car::Remove(0);
                host = {};
            }
        }
    }
    if (g_net || g_searching) Receive(now);
    if (!g_reject.empty()) {
        std::string error = std::move(g_reject);
        g_reject.clear();
        StopSession();
        PublishSession(error);
    }
    if (g_searching) {
        if (now >= g_search_until) {
            g_searching = false;
            if (!g_net) net::Close();
        } else if (!g_search_tx || now - g_search_tx >= 500) {
            Hello query{Make(kDiscover, 0, now), {}};
            std::memcpy(query.xbe_sha1, kImageSha1, 40);
            net::Send({0xFFFFFFFFu, g_search_port}, &query, sizeof query);
            net::Send({net::Resolve("127.0.0.1"), g_search_port}, &query, sizeof query);
            g_search_tx = now;
        }
    }
    if (g_rescue.exchange(false)) Rescue();
    mod::car::SetGrid(g_net && g_cfg.role == Role::Join && g_local_player < kMaxPlayers ? g_local_player : 0);
    if (g_net) Send(now);
    PublishSession();
}

const char* Status() {
    static char s[32];
    uint64_t now = NowMs();
    int partner = -1;
    for (int i = 0; i < kMaxPlayers && partner < 0; i++)
        if (Connected(i)) partner = i;
    if (!g_net) std::snprintf(s, sizeof s, "%s", mod::control::Enabled() ? "COOP CONTROL" : "COOP OFF");
    else if (g_cfg.online && partner < 0) std::snprintf(s, sizeof s, "%s", online::Status().c_str());
    else if (partner < 0) std::snprintf(s, sizeof s, "COOP HOST %u WAITING", g_cfg.port);
    else if (g_cfg.role == Role::Join && !g_players[0].welcomed) std::snprintf(s, sizeof s, "COOP CONNECTING");
    else if (now - g_players[partner].last_rx > kTimeoutMs) std::snprintf(s, sizeof s, "COOP NO PARTNER");
    else if (!g_players[partner].remote.in_play) std::snprintf(s, sizeof s, "COOP %d/8 PARTNER IN MENUS", PlayerCount());
    else if (g_players[partner].remote.city != mod::car::City()) std::snprintf(s, sizeof s, "COOP %d/8 OTHER CITY", PlayerCount());
    else std::snprintf(s, sizeof s, "COOP %d/8 CONNECTED", PlayerCount());
    return s;
}

}  // namespace coop
