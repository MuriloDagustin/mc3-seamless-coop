// Host-authoritative ambient traffic (traffic.h, docs/RE-TRAFEGO.txt).
// Pool: tm = [[0x50152C]+8], n = s16 tm+0x12C, slot i = [tm+0x130] + i*0x21C. Per slot: +0x2C u16 render/level
// handle (0xFFFF: not spawned), +0x34 matrix (right, up, -forward rows, position +0x58), +0x74 previous matrix,
// +0xB5 model variant, +0xB1 LOD band, +0xC4 race-controlled, +0xC8 front point, +0xD4 heading, +0xE0 speed,
// +0xEC trailer (semi trucks), +0xFC/+0x100 road links, +0x138 type, +0x13C crash body, +0x14E, +0x16C colour.
#include "traffic.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "mod.h"

namespace mod::traffic {

namespace {

constexpr uint32_t kRoot = 0x50152C, kSlotSize = 0x21C, kTmVtable = 0x44CE90, kSlotVtable = 0x44D1CC;
constexpr uint32_t kSetupCurrent = 0x53F2E0;
constexpr uint64_t kSendMs = 50, kStaleMs = 400;
constexpr float kHiddenY = -500.0f;

bool Ptr(uint32_t va) { return va >= 0x10000 && Mapped(va, 4); }
uint32_t P(uint32_t va) { return Ptr(va) ? R32(va) : 0; }

struct Pool {
    uint32_t tm = 0, slots = 0;
    int n = 0;
    uint32_t layout = 0;
};

// the pool, once its Init has finished (docs/RE-TRAFEGO.txt pool-identity A), and its layout hash: the size, each
// slot's type index and model variant, the city and the time of day
bool ReadPool(Pool& p) {
    uint32_t tm = P(P(kRoot) + 8);
    if (!tm || P(tm) != kTmVtable || !Mapped(tm + 0x12C, 0x14)) return false;
    int n = G<int16_t>(tm + 0x12C);
    uint32_t slots = P(tm + 0x130);
    if (n <= 0 || n > 1024 || !slots || !Mapped(slots, uint32_t(n) * kSlotSize) || P(slots) != kSlotVtable) return false;
    if (P(tm + 0x13C) != uint32_t(n / 30 + 1)) return false;
    uint32_t types = P(tm + 0x30);
    int ntypes = G<uint16_t>(tm + 0x34);
    uint32_t setup = P(kSetupCurrent);
    uint32_t h = 2166136261u;
    auto mix = [&](uint32_t v) { h = (h ^ v) * 16777619u; };
    mix(uint32_t(n)), mix(setup ? P(setup) : 0xFFFF), mix(setup ? P(setup + 4) : 0xFFFF);
    for (int i = 0; i < n; i++) {
        uint32_t s = slots + uint32_t(i) * kSlotSize, type = P(s + 0x138);
        int ti = -1;
        for (int j = 0; j < ntypes && types; j++)
            if (P(types + uint32_t(j) * 4) == type) {
                ti = j;
                break;
            }
        mix(uint32_t(ti)), mix(G<uint8_t>(s + 0xB5));
    }
    p = {tm, slots, n, h};
    return true;
}

bool Active(uint32_t s) {
    return G<uint16_t>(s + 0x2C) != 0xFFFF && (P(s + 0xFC) || P(s + 0xC4) == 1 || P(s + 0xEC));
}

int16_t Q(float v) { return int16_t(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 32767.0f)); }
float F(int16_t v) { return float(v) / 32767.0f; }

std::mutex g_lock;
// host
uint32_t g_cycle = 0;
uint64_t g_sent_at = 0;
// partner
struct Remote {
    Car car{};
    uint64_t at = 0;  // ours, ms
};
std::vector<Remote> g_remote;
uint32_t g_host_layout = 0, g_mismatch_logged = 0;
uint32_t g_tm = 0;  // the pool the puppets belong to
std::vector<uint8_t> g_puppet;  // slots this mod took over (unlinked)
int g_shown = 0, g_hidden = 0, g_frames = 0;
bool g_active = false;

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

}  // namespace

bool Snapshot(std::vector<std::vector<uint8_t>>& packets, uint64_t now) {
    if (now - g_sent_at < kSendMs) return false;
    Pool p;
    if (!ReadPool(p)) return false;
    g_sent_at = now;
    std::vector<Car> cars;
    for (int i = 0; i < p.n; i++) {
        uint32_t s = p.slots + uint32_t(i) * kSlotSize;
        if (!Active(s)) continue;
        Car c{};
        c.slot = uint16_t(i), c.flags = 1, c.colour = G<uint8_t>(s + 0x16C);
        for (int k = 0; k < 3; k++) {
            c.pos[k] = RF(s + 0x58 + k * 4);
            c.fwd[k] = Q(-RF(s + 0x4C + k * 4));
            c.up[k] = Q(RF(s + 0x40 + k * 4));
        }
        c.speed = RF(s + 0xE0);
        if (!std::isfinite(c.pos[0] + c.pos[1] + c.pos[2] + c.speed)) continue;
        cars.push_back(c);
    }
    int chunks = int((cars.size() + kCarsPerPacket - 1) / kCarsPerPacket);
    if (!chunks) chunks = 1;
    uint32_t cycle = ++g_cycle;
    packets.clear();
    for (int k = 0; k < chunks; k++) {
        size_t first = size_t(k) * kCarsPerPacket, count = std::min(cars.size() - std::min(cars.size(), first), size_t(kCarsPerPacket));
        Header h{p.layout, cycle, uint16_t(p.n), uint8_t(k), uint8_t(chunks), uint8_t(count), {}};
        std::vector<uint8_t> out(sizeof h + count * sizeof(Car));
        std::memcpy(out.data(), &h, sizeof h);
        if (count) std::memcpy(out.data() + sizeof h, cars.data() + first, count * sizeof(Car));
        packets.push_back(std::move(out));
    }
    return true;
}

void Receive(const uint8_t* data, int bytes) {
    if (bytes < int(sizeof(Header))) return;
    Header h;
    std::memcpy(&h, data, sizeof h);
    if (bytes != int(sizeof h + h.cars * sizeof(Car)) || h.count == 0 || h.count > 1024) return;
    std::lock_guard lock(g_lock);
    g_host_layout = h.layout;
    if (g_remote.size() != h.count) g_remote.assign(h.count, {});
    uint64_t now = GetTickCount64();
    for (int i = 0; i < h.cars; i++) {
        Car c;
        std::memcpy(&c, data + sizeof h + i * sizeof(Car), sizeof c);
        if (c.slot < h.count) g_remote[c.slot] = {c, now};
    }
}

void Tick(bool partner, uint64_t now) {
    std::lock_guard lock(g_lock);
    if (!partner || g_remote.empty()) return;
    Pool p;
    if (!ReadPool(p)) return;
    if (p.layout != g_host_layout || int(g_remote.size()) != p.n) {
        if (g_mismatch_logged != g_host_layout) {
            g_mismatch_logged = g_host_layout;
            Log("traffic: the host's pool differs from this one (%08X / %08X, %d / %zu cars): not synced", g_host_layout,
                p.layout, p.n, g_remote.size());
        }
        g_active = false;
        return;
    }
    if (p.tm != g_tm) {  // a new pool (a city or race load): nothing of ours in it yet
        g_tm = p.tm;
        g_puppet.assign(size_t(p.n), 0);
        Log("traffic: syncing %d cars with the host", p.n);
    }
    g_active = true;
    int shown = 0, hidden = 0;
    for (int i = 0; i < p.n; i++) {
        uint32_t s = p.slots + uint32_t(i) * kSlotSize;
        if (G<uint16_t>(s + 0x2C) == 0xFFFF) continue;  // not spawned here: nothing to draw yet
        if (P(s + 0xEC) || P(s + 0xC4) == 1 || P(s + 0x13C)) continue;  // trucks, race cars, crashes: left local
        const Remote& r = g_remote[size_t(i)];
        bool live = r.at && now - r.at < kStaleMs && (r.car.flags & 1);
        if (!live && !g_puppet[size_t(i)] && !P(s + 0xFC)) continue;
        g_puppet[size_t(i)] = 1;
        if (live) {
            Apply(s, r.car, float(now - r.at) / 1000.0f);
            shown++;
        } else {
            W32(s + 0xFC, 0), W32(s + 0x100, 0);
            Hide(s);
            hidden++;
        }
    }
    g_shown = shown, g_hidden = hidden, g_frames++;
}

void Stop() {
    std::lock_guard lock(g_lock);
    g_remote.clear();
    g_active = false;
}

std::string Describe() {
    std::lock_guard lock(g_lock);
    char s[160];
    std::snprintf(s, sizeof s, "traffic=%s shown=%d hidden=%d host_cars=%zu", g_active ? "on" : "off", g_shown, g_hidden,
                  g_remote.size());
    return s;
}

}  // namespace mod::traffic
