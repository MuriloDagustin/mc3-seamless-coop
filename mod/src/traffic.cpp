// Host-authoritative ambient traffic (traffic.h, docs/RE-TRAFEGO.txt).
// Pool: tm = [[0x50152C]+8], n = s16 tm+0x12C, slot i = [tm+0x130] + i*0x21C. Per slot: +0x2C u16 render/level
// handle (0xFFFF: not spawned), +0x34 matrix (right, up, -forward rows, position +0x58), +0x74 previous matrix,
// +0xB5 model variant, +0xB1 LOD band, +0xC4 race-controlled, +0xC8 front point, +0xD4 heading, +0xE0 speed,
// +0xEC trailer (semi trucks), +0xFC/+0x100 road links, +0x138 type, +0x13C crash collider, +0x14E, +0x16C colour.
#include "traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "mod.h"
#include "car.h"

namespace mod::traffic {

namespace {

constexpr uint32_t kRoot = 0x50152C, kSlotSize = 0x21C, kTmVtable = 0x44CE90, kSlotVtable = 0x44D1CC;
constexpr uint32_t kSetupCurrent = 0x53F2E0;
constexpr uint64_t kSendMs = 50, kStaleMs = 400;
constexpr float kHiddenY = -500.0f;

bool Valid(uint32_t va, uint32_t bytes = 4) {
    return va >= 0x10000 && bytes && va <= UINT32_MAX - bytes && Mapped(va, bytes);
}
bool Ptr(uint32_t va) { return Valid(va); }
uint32_t P(uint32_t va) { return Ptr(va) ? R32(va) : 0; }

struct Pool {
    uint32_t tm = 0, slots = 0;
    int n = 0;
    uint32_t layout = 0;
    uint32_t world = 0, race = 0;
};

// the pool, once its Init has finished (docs/RE-TRAFEGO.txt pool-identity A), and its layout hash: the size, each
// slot's type index and model variant, the city and the time of day
bool ReadPool(Pool& p) {
    uint32_t tm = P(P(kRoot) + 8);
    if (!tm || P(tm) != kTmVtable || !Mapped(tm + 0x12C, 0x14)) return false;
    int n = G<int16_t>(tm + 0x12C);
    uint32_t slots = P(tm + 0x130);
    if (n <= 0 || n > 1024 || !Valid(slots, uint32_t(n) * kSlotSize) || P(slots) != kSlotVtable) return false;
    if (P(tm + 0x13C) != uint32_t(n / 30 + 1)) return false;
    uint32_t types = P(tm + 0x30);
    int ntypes = G<uint16_t>(tm + 0x34);
    uint32_t setup = P(kSetupCurrent);
    if (ntypes <= 0 || ntypes > 1024 || !Valid(types, uint32_t(ntypes) * 4) || !Valid(setup, 0xE8)) return false;
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(uint32_t(n)), mix(setup ? P(setup) : 0xFFFF), mix(setup ? P(setup + 4) : 0xFFFF);
    for (int i = 0; i < n; i++) {
        uint32_t s = slots + uint32_t(i) * kSlotSize, type = P(s + 0x138);
        if (P(s) != kSlotVtable || !Valid(type, 0x29C)) return false;
        int ti = -1;
        for (int j = 0; j < ntypes && types; j++)
            if (P(types + uint32_t(j) * 4) == type) {
                ti = j;
                break;
            }
        if (ti < 0) return false;
        mix(uint32_t(ti)), mix(G<uint8_t>(s + 0xB5));
    }
    uint32_t world = 2166136261u;
    auto key = [&](uint32_t v) { world = (world ^ v) * 16777619u; };
    key(P(setup)); key(P(setup + 4));
    bool terminated = false;
    for (unsigned i = 0; i < 128; i++) {
        uint8_t c = G<uint8_t>(setup + 0x68 + i); key(c);
        if (!c) { terminated = true; break; }
    }
    if (!terminated) return false;
    p = {tm, slots, n, h, world, P(P(0x533E24) + 4)};
    return true;
}

bool Active(uint32_t s) {
    return G<uint16_t>(s + 0x2C) != 0xFFFF && (P(s + 0xFC) || P(s + 0xC4) == 1 || P(s + 0xEC) || P(s + 0x13C));
}

int16_t Q(float v) { return int16_t(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 32767.0f)); }
float F(int16_t v) { return float(v) / 32767.0f; }

std::mutex g_lock;
// host
uint32_t g_cycle = 0;
uint64_t g_sent_at = 0;
std::vector<std::vector<uint8_t>> g_publish;
uint32_t g_published_cycle = 0;
uint32_t g_epoch = 0, g_source_tm = 0, g_source_slots = 0, g_source_race = 0, g_source_world = 0;
// partner
struct Remote {
    Car car{};
    uint64_t at = 0;  // ours, ms
};
std::vector<Remote> g_remote;
struct Batch { Header h{}; std::vector<Car> cars; std::vector<bool> chunks; uint64_t at = 0; };
Batch g_pending;
Header g_ready{};
uint64_t g_ready_at = 0;
uint32_t g_host_layout = 0, g_mismatch_logged = 0;
uint32_t g_tm = 0;  // the pool the puppets belong to
uint32_t g_slots = 0;
struct Puppet {
    uint32_t road = 0, next = 0; uint16_t handle = 0xFFFF; bool owned = false;
    uint32_t type = 0, trailer = 0, scripted = 0;
    uint8_t route = 0, lod = 0, stopped = 0;
    bool physics = false;
    Car visible{}; bool have_visible = false, hidden = false;
};
std::vector<Puppet> g_puppet;
int g_shown = 0, g_hidden = 0, g_frames = 0;
bool g_active = false;
int g_trailers = 0, g_bodies = 0, g_proxies = 0, g_blocked = 0;
uint32_t g_owned_race = 0, g_owned_world = 0, g_owned_epoch = 0;

// Kept opt-in until straight/turn/contact/reset tests can run silently. The released ZIP stays unchanged.
bool PhysicsEnabled() {
    char value[8]{};
    return GetEnvironmentVariableA("MC3COOP_TRAFFIC_PHYSICS", value, sizeof value) == 1 && value[0] == '1';
}

bool ApplyPhysics(uint32_t s, const Car& c, float ahead);

bool Playable() {
    uint32_t machine = P(0x557EEC), game = P(0x557ED0);
    int state = int(P(machine + 4));
    return state >= 5 && state <= 6 && P(game + 4) != 3 && car::InPlay();
}

// Reset skips a car with no road, no script and no trailer. Relink our cars BEFORE that
// reset, otherwise renderer clearing leaves used pool slots with handle FFFF forever.
// Never restore an old road into a new pool or a reallocated renderer object.
void Release(const Pool& p) {
    if (p.tm == g_tm && p.slots == g_slots && p.race == g_owned_race && p.world == g_owned_world &&
        int(g_puppet.size()) == p.n) {
        for (int i = 0; i < p.n; i++) {
            const Puppet& saved = g_puppet[size_t(i)];
            uint32_t s = p.slots + uint32_t(i) * kSlotSize;
            if (!saved.owned || G<uint16_t>(s + 0x2C) != saved.handle || P(s + 0x138) != saved.type ||
                P(s + 0xEC) != saved.trailer) continue;
            if (saved.physics) {
                // Restore only our scripted state. Native collision/body ownership stays entirely native.
                if (P(s + 0xC4) == 1 && !G<uint8_t>(s + 0xAC) && !P(s + 0xA8)) {
                    if (saved.hidden && saved.have_visible) ApplyPhysics(s, saved.visible, 0);
                    W32(s + 0xC4, saved.scripted); G<uint8_t>(s + 0xAC) = saved.route;
                    G<uint8_t>(s + 0xB1) = saved.lod; G<uint8_t>(s + 0xB2) = saved.stopped;
                }
            } else if (!P(s + 0xFC) && !P(s + 0x100) && !P(s + 0xC4) && !P(s + 0x13C)) {
                W32(s + 0xFC, saved.road);
                W32(s + 0x100, saved.next);
            }
        }
    }
    g_puppet.clear();
    g_tm = g_slots = 0;
    g_owned_race = g_owned_world = g_owned_epoch = 0;
    g_active = false;
    g_shown = g_hidden = 0;
    g_trailers = g_bodies = g_proxies = g_blocked = 0;
}

void Hide(uint32_t s) {
    float y = kHiddenY;
    std::memcpy(HostPtr(s + 0x5C), &y, 4);
    std::memcpy(HostPtr(s + 0x9C), &y, 4);
    G<float>(s + 0xCC) = y;
    G<float>(s + 0xE0) = 0.0f;
}

// a car of the partner's pool driven by the host's slot: unlinked from the roads, placed and turned
void Apply(uint32_t s, const Car& c, float ahead) {
    W32(s + 0xFC, 0), W32(s + 0x100, 0);  // no road: no drive, no lane change, no recycle (0x80D10, 0x6A5C0)
    G<uint8_t>(s + 0xB1) = 0, G<uint8_t>(s + 0x14E) = 0;
    W32(s + 0xA8, 0), W32(s + 0xB8, 0);
    float fwd[3] = {F(c.fwd[0]), F(c.fwd[1]), F(c.fwd[2])}, up[3] = {F(c.up[0]), F(c.up[1]), F(c.up[2])};
    float right[3] = {up[1] * -fwd[2] - up[2] * -fwd[1], up[2] * -fwd[0] - up[0] * -fwd[2], up[0] * -fwd[1] - up[1] * -fwd[0]};
    float pos[3];
    for (int i = 0; i < 3; i++) pos[i] = c.pos[i] + fwd[i] * c.speed * ahead;
    float m[12] = {right[0], right[1], right[2], up[0], up[1], up[2], -fwd[0], -fwd[1], -fwd[2], pos[0], pos[1], pos[2]};
    std::memcpy(HostPtr(s + 0x34), m, sizeof m);
    std::memcpy(HostPtr(s + 0x74), m, sizeof m);
    float length = 0;  // the front point leads the position by the type's length (0x767B0)
    uint32_t type = P(s + 0x138);
    if (Ptr(type + 0x298)) length = RF(type + 0x298);
    for (int i = 0; i < 3; i++) G<float>(s + 0xC8 + i * 4) = pos[i] + fwd[i] * length, G<float>(s + 0xD4 + i * 4) = fwd[i];
    G<float>(s + 0xE0) = c.speed;
    G<uint8_t>(s + 0x16C) = c.colour;
}

#include "traffic_physics.inc"

}  // namespace

void Capture(uint64_t now) {
    std::lock_guard lock(g_lock);
    static uint32_t logged = 0;
    auto note = [&](uint32_t state, const char* text) {
        if (logged != state) { logged = state; Log("traffic: capture %s", text); }
    };
    if (!Playable()) { note(1, "off (not playable)"); g_source_tm = 0; g_publish.clear(); return; }
    if (now < g_sent_at || now - g_sent_at < kSendMs) return;
    Pool p;
    if (!ReadPool(p)) { note(2, "off (pool not ready)"); g_publish.clear(); return; }
    if (p.tm != g_source_tm || p.slots != g_source_slots || p.race != g_source_race || p.world != g_source_world) {
        if (!++g_epoch) ++g_epoch;
        g_source_tm = p.tm; g_source_slots = p.slots; g_source_race = p.race; g_source_world = p.world;
    }
    std::vector<Car> cars(size_t(p.n));
    for (int i = 0; i < p.n; i++) {
        uint32_t s = p.slots + uint32_t(i) * kSlotSize;
        if (!ReadCar(s, uint16_t(i), cars[size_t(i)])) { note(3, "off (slot read failed)"); g_publish.clear(); return; }
    }
    // Avoid publishing a mixed pool while the session thread samples a load/reset.
    Pool after;
    if (!Playable() || !ReadPool(after) || after.tm != p.tm || after.slots != p.slots ||
        after.race != p.race || after.layout != p.layout || after.world != p.world) {
        note(4, "off (pool changed during capture)");
        g_publish.clear(); return;
    }
    note(5, "on");
    g_sent_at = now;
    int chunks = int((cars.size() + kCarsPerPacket - 1) / kCarsPerPacket);
    if (!chunks) chunks = 1;
    if (!++g_cycle) ++g_cycle;
    uint32_t cycle = g_cycle;
    std::vector<std::vector<uint8_t>> packets;
    for (int k = 0; k < chunks; k++) {
        size_t first = size_t(k) * kCarsPerPacket, count = std::min(cars.size() - std::min(cars.size(), first), size_t(kCarsPerPacket));
        Header h{p.layout, p.world, g_epoch, cycle, uint16_t(p.n), uint8_t(k), uint8_t(chunks), uint8_t(count), 2, {}};
        std::vector<uint8_t> out(sizeof h + count * sizeof(Car));
        std::memcpy(out.data(), &h, sizeof h);
        if (count) std::memcpy(out.data() + sizeof h, cars.data() + first, count * sizeof(Car));
        packets.push_back(std::move(out));
    }
    g_publish = std::move(packets);
}

bool Snapshot(std::vector<std::vector<uint8_t>>& packets, uint64_t now) {
    (void)now;
    std::lock_guard lock(g_lock);
    // Capture timestamps come from GetTickCount64 on the game thread. The session thread uses steady_clock for
    // protocol timers, so comparing its `now` with g_sent_at rejected every snapshot (traffic stayed off). Use the
    // same clock as Capture here; pose interpolation keeps its own offset conversion.
    uint64_t clock = GetTickCount64();
    if (g_publish.empty() || g_published_cycle == g_cycle || clock < g_sent_at || clock - g_sent_at >= kStaleMs)
        return false;
    packets = g_publish; g_published_cycle = g_cycle;
    return true;
}

void Receive(const uint8_t* data, int bytes) {
    if (!data || bytes < int(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, data, sizeof h);
    static uint32_t logged_epoch = 0, logged_reject = 0;
    if (h.version != 2 || h.pad[0] || h.pad[1] || !h.epoch || !h.cycle ||
        bytes != int(sizeof h + h.cars * sizeof(Car)) || !h.count || h.count > 1024 ||
        h.chunks != (h.count + kCarsPerPacket - 1) / kCarsPerPacket || h.chunk >= h.chunks ||
        h.cars != std::min(kCarsPerPacket, int(h.count) - int(h.chunk)*kCarsPerPacket)) {
        if (!logged_reject) { logged_reject = 1; Log("traffic: recv rejected a malformed packet (%d bytes)", bytes); }
        return;
    }
    logged_reject = 0;
    std::vector<Car> incoming(h.cars);
    std::memcpy(incoming.data(), data + sizeof h, incoming.size()*sizeof(Car));
    for (unsigned i = 0; i < incoming.size(); i++)
        if (incoming[i].slot != unsigned(h.chunk)*kCarsPerPacket+i || !Finite(incoming[i])) return;
    std::lock_guard lock(g_lock);
    uint64_t now = GetTickCount64();
    auto older = [](const Header& a, const Header& b) {
        return int32_t(a.epoch-b.epoch) < 0 || (a.epoch == b.epoch && int32_t(a.cycle-b.cycle) <= 0);
    };
    if (g_ready.epoch && older(h, g_ready)) return;
    if (g_pending.h.epoch && (int32_t(h.epoch-g_pending.h.epoch) < 0 ||
        (h.epoch == g_pending.h.epoch && int32_t(h.cycle-g_pending.h.cycle) < 0))) return;
    if (!g_pending.h.epoch || h.epoch != g_pending.h.epoch || h.cycle != g_pending.h.cycle) {
        g_pending = {h, std::vector<Car>(h.count), std::vector<bool>(h.chunks), now};
    }
    if (h.layout != g_pending.h.layout || h.world != g_pending.h.world || h.count != g_pending.h.count ||
        h.chunks != g_pending.h.chunks || g_pending.chunks[h.chunk]) return;
    std::copy(incoming.begin(), incoming.end(), g_pending.cars.begin() + h.chunk*kCarsPerPacket);
    g_pending.chunks[h.chunk] = true;
    if (!std::all_of(g_pending.chunks.begin(), g_pending.chunks.end(), [](bool got) { return got; })) return;
    if (now < g_pending.at || now - g_pending.at >= kStaleMs) { g_pending = {}; return; }
    g_remote.resize(h.count);
    for (unsigned i = 0; i < h.count; i++) g_remote[i] = {g_pending.cars[i], now};
    g_host_layout = h.layout; g_ready = h; g_ready_at = now; g_pending = {};
    if (logged_epoch != h.epoch) {
        logged_epoch = h.epoch;
        Log("traffic: received batch epoch=%u cycle=%u cars=%u layout=%08X world=%08X", h.epoch, h.cycle, h.count,
            h.layout, h.world);
    }
}

void Tick(bool partner, uint64_t now) {
    std::lock_guard lock(g_lock);
    Pool p;
    if (!ReadPool(p)) {
        // Do not touch saved addresses. A transient load/diagnostic failure must not forget a still-owned C4
        // and strand it forever; Release will validate the full identity once a pool is readable again.
        g_active = false; g_shown = g_hidden = 0; return;
    }
    if (!partner || g_remote.empty() || !Playable()) { Release(p); return; }
    if (now < g_ready_at || now - g_ready_at >= kStaleMs) { Release(p); return; }
    if (p.layout != g_host_layout || p.world != g_ready.world || int(g_remote.size()) != p.n) {
        if (g_mismatch_logged != g_host_layout) {
            g_mismatch_logged = g_host_layout;
            Log("traffic: the host's pool differs from this one (%08X / %08X, %d / %zu cars): not synced", g_host_layout,
                p.layout, p.n, g_remote.size());
        }
        Release(p);
        return;
    }
    bool physics = PhysicsEnabled();
    if (p.tm != g_tm || p.slots != g_slots || p.race != g_owned_race || p.world != g_owned_world ||
        g_ready.epoch != g_owned_epoch || g_puppet.size() != size_t(p.n) ||
        (!g_puppet.empty() && std::any_of(g_puppet.begin(), g_puppet.end(),
            [&](const Puppet& s) { return s.owned && s.physics != physics; }))) {
        Release(p);
        g_tm = p.tm; g_slots = p.slots;
        g_owned_race = p.race; g_owned_world = p.world; g_owned_epoch = g_ready.epoch;
        g_puppet.assign(size_t(p.n), {});
        Log("traffic: syncing %d cars with the host", p.n);
    }
    int shown = 0, hidden = 0;
    g_trailers = g_bodies = g_proxies = g_blocked = 0;
    for (int i = 0; i < p.n; i++) {
        uint32_t s = p.slots + uint32_t(i) * kSlotSize;
        if (G<uint16_t>(s + 0x2C) == 0xFFFF) continue;  // not spawned here: nothing to draw yet
        const Remote& r = g_remote[size_t(i)];
        Puppet& saved = g_puppet[size_t(i)];
        if (saved.owned && (saved.handle != G<uint16_t>(s + 0x2C) || saved.type != P(s + 0x138) ||
            saved.trailer != P(s + 0xEC))) saved = {};
        bool live = r.at && now >= r.at && now - r.at < kStaleMs && (r.car.flags & kActive);
        if (physics) {
            if (P(s + 0xA8) || (P(s + 0xC4) && !saved.owned) ||
                (saved.owned && (P(s + 0xC4) != 1 || G<uint8_t>(s + 0xAC)))) {
                g_blocked++; continue;
            }
            Puppet candidate = saved;
            if (!candidate.owned) {
                candidate.road = P(s + 0xFC); candidate.next = P(s + 0x100);
                candidate.handle = G<uint16_t>(s + 0x2C); candidate.type = P(s + 0x138);
                candidate.trailer = P(s + 0xEC); candidate.scripted = P(s + 0xC4);
                candidate.route = G<uint8_t>(s + 0xAC); candidate.lod = G<uint8_t>(s + 0xB1);
                candidate.stopped = G<uint8_t>(s + 0xB2); candidate.owned = candidate.physics = true;
            }
            Car desired = r.car;
            if (!live) {
                if (!candidate.have_visible) {
                    if (!ReadCar(s, uint16_t(i), candidate.visible) || !(candidate.visible.flags & kActive)) {
                        g_blocked++; continue;
                    }
                    candidate.have_visible = true;
                }
                desired = candidate.visible;
                // Hide both physical bodies and all articulated bounds, not only the tractor mesh.
                float dy = kHiddenY-desired.pos[1]; desired.pos[1] += dy; desired.centre[1] += dy;
                desired.trailer[10] += dy; desired.trailer_centre[1] += dy;
                std::memset(desired.velocity, 0, sizeof desired.velocity);
                std::memset(desired.angular, 0, sizeof desired.angular);
                std::memset(desired.trailer_velocity, 0, sizeof desired.trailer_velocity);
                std::memset(desired.trailer_angular, 0, sizeof desired.trailer_angular);
                desired.speed = 0;
            }
            if (!ApplyPhysics(s, desired, live ? std::min(float(now-r.at)/1000.f, 0.10f) : 0.f)) {
                g_blocked++; continue;
            }
            if (live) { candidate.visible = r.car; candidate.have_visible = true; shown++; }
            else hidden++;
            candidate.hidden = !live; saved = candidate;
            continue;
        }
        if (P(s + 0xEC) || P(s + 0xC4) == 1 || P(s + 0x13C)) continue;  // legacy path
        if (!live && !saved.owned && !P(s + 0xFC)) continue;
        if (!saved.owned) {
            saved = {P(s + 0xFC), P(s + 0x100), G<uint16_t>(s + 0x2C), true};
            saved.type = P(s + 0x138); saved.trailer = P(s + 0xEC);
        }
        if (live) {
            Apply(s, r.car, float(now - r.at) / 1000.0f);
            shown++;
        } else {
            W32(s + 0xFC, 0), W32(s + 0x100, 0);
            Hide(s);
            hidden++;
        }
    }
    g_active = shown > 0;
    g_shown = shown, g_hidden = hidden, g_frames++;
}

void Stop() {
    std::lock_guard lock(g_lock);
    g_remote.clear();
    g_pending = {}; g_ready = {}; g_ready_at = 0;
    g_source_tm = g_source_slots = g_source_race = g_source_world = 0;
    g_sent_at = 0;
    g_publish.clear(); g_published_cycle = 0;
    g_active = false;
}

std::string Describe() {
    std::lock_guard lock(g_lock);
    Pool p;
    bool ready = ReadPool(p);
    uint32_t tm = P(P(kRoot) + 8), setup = P(kSetupCurrent);
    int spawned = 0, roads = 0, wrecks = 0;
    if (ready) for (int i = 0; i < p.n; i++) {
        uint32_t slot = p.slots + uint32_t(i) * kSlotSize;
        if (G<uint16_t>(slot + 0x2C) == 0xFFFF) continue;
        spawned++;
        roads += P(slot + 0xFC) != 0;
        wrecks += P(slot + 0x13C) != 0;
    }
    int views = int(P(setup + 0x50));
    // 0x6CDCD..0x6CDFB: the density-table index is LOCAL VIEWS-1, not the human racer count.
    int target = -1;
    if (tm && Mapped(tm, 0x1A0)) {
        uint32_t offset = P(setup + 8) == 2 ? 0x70 : 0x68;
        if (Mapped(setup + 0x108, 1) && G<uint8_t>(setup + 0x108)) offset = P(setup + 8) == 2 ? 0x7C : 0x78;
        else if (views >= 1 && views <= 2) offset += uint32_t(views - 1) * 4;
        else offset = 0;
        if (offset) target = int(R32(tm + offset));
    }
    uint32_t regions = P(P(0x5587E0) + 0x5C), session = P(0x557EFC);
    auto byte = [](uint32_t va) { return Ptr(va) ? int(G<uint8_t>(va)) : -1; };
    uint32_t links = 0;
    for (uint32_t i = 0; i < std::min(P(regions + 0x18), 8u); i++) {
        uint32_t count = regions + 0x20 + i * 8;
        if (Ptr(count)) links += G<uint16_t>(count);
    }
    char s[640];
    std::snprintf(s, sizeof s,
        "traffic=%s shown=%d hidden=%d host_cars=%zu pool_ready=%d pool=%d spawned=%d roads=%d wrecks=%d "
        "spawn_gate=%d frozen=%d target=%d humans=%u views=%d native_net=%d session=%d/%d regions=%u links=%u "
        "physics=%d trailers=%d bodies=%d proxies=%d blocked=%d epoch=%u cycle=%u "
        "publish=%zu source_tm=%08X sent=%llu layout=%08X host_layout=%08X ready=%u/%u",
        g_active ? "on" : (ready && !spawned && !g_remote.empty() ? "waiting_spawn" : "off"),
        g_shown, g_hidden, g_remote.size(), ready, p.n, spawned, roads, wrecks,
        byte(0x4AFCCC), tm ? byte(tm + 0x144) : -1, target, P(setup + 0x4C), views,
        setup ? byte(setup + 0x108) : -1, int32_t(P(session + 0x10)), int32_t(P(session + 0x18)),
        P(regions + 0x18), links, PhysicsEnabled(), g_trailers, g_bodies, g_proxies, g_blocked, g_ready.epoch, g_ready.cycle,
        g_publish.size(), g_source_tm, (unsigned long long)g_sent_at, ready ? p.layout : 0, g_host_layout,
        g_ready.epoch, g_ready.cycle);
    return s;
}

}  // namespace mod::traffic
