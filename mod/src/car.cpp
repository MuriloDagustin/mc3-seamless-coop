// MC3's cars for the co-op, in the runner's guest memory (car.h, docs/RE-MAPA.txt, docs/RE-REMOTO.txt).
//
// A partner is a second human of the host's own career race/cruise, without the game's network mode (that mode
// stops the AI, the traffic, the finish, the money, the career credit and the autosave): both race setups (current
// [0x53F2E0] and next [0x53F2E4]) say 2 humans, and slot 1 holds the partner's own campaign car (its CarCfg) with no
// pad/camera/HUD (+0x10, +0x14, +0x18 = -1). A second HUD would force split-screen and use a missing camera.
// race.cpp completes the remote racer locally without its native finish callback; its real result arrives over
// the network. The game still builds its car, driven by the partner's packets.
#include "car.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>

#include "mod.h"

namespace mod::car {

namespace {

constexpr uint32_t kPlayerManager = 0x558814;  // +0x08 players[8], +0x28 count
constexpr uint32_t kCareer = 0x557F0C;         // mcPgCareer*, +0x0C career data (+0x50 current city)
constexpr uint32_t kSetupCurrent = 0x53F2E0, kSetupNext = 0x53F2E4;
constexpr uint32_t kSetupHumans = 0x4C, kSetupViews = 0x50, kSetupNetwork = 0x108;
constexpr uint32_t kSlot = 0x114, kSlotSize = 0x2C;  // slot i at setup + 0x114 + i * 0x2C
constexpr uint32_t kSlotCar = 0x0C, kSlotPad = 0x10, kSlotHud = 0x14, kSlotPlayer = 0x18, kSlotOut = 0x26;
constexpr uint32_t kGarage = 0x53F810, kGarageCount = 0x53F80C, kGarageSelected = 0x53F80D;

bool Ptr(uint32_t va) { return va >= 0x10000 && Mapped(va, 4); }

struct Chain {
    uint32_t car = 0, inst = 0, body = 0;
};

constexpr uint32_t kTypeName = 0xE3, kTypeNameBytes = 0x16, kLiveDirty = 0x3F7F;
bool ConfigPtr(uint32_t p) { return p >= 0x10000 && !(p & 3) && p <= UINT32_MAX-kCarCfgBytes && Mapped(p,kCarCfgBytes); }
bool ValidName(const uint8_t* name) {
    if (!name) return false;
    for (uint32_t i=0;i<kTypeNameBytes;i++) {
        uint8_t c=name[i];
        if (!c) return i>0;
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_')) return false;
    }
    return false;
}
bool ValidType(const uint8_t* cfg) { return cfg&&ValidName(cfg+kTypeName); }
bool ValidConfig(const uint8_t* cfg) {
    return ValidType(cfg) && std::memchr(cfg+0xF9,0,13)!=nullptr;
}
bool SameType(const uint8_t* a,const uint8_t* b) {
    for (uint32_t i=0;i<kTypeNameBytes;i++) {
        if(a[kTypeName+i]!=b[kTypeName+i])return false;
        if(!a[kTypeName+i])return true;
    }
    return false;
}
bool SameResources(const uint8_t* a,const uint8_t* b) {
    // 20566..205BB also requires a reload for E2 boolean / F9 string changes, even at the same TypeName.
    if(!SameType(a,b)||bool(a[0xE2])!=bool(b[0xE2]))return false;
    for(unsigned i=0;i<13;i++) {
        if(a[0xF9+i]!=b[0xF9+i])return false;
        if(!a[0xF9+i])return true;
    }
    return false;
}
// Native dirty bits and the local vtable are not part of the remote configuration's identity.
bool SameConfig(const uint8_t* a,const uint8_t* b) {
    return !std::memcmp(a+4,b+4,4) && !std::memcmp(a+0x0C,b+0x0C,kCarCfgBytes-0x0C);
}
uint32_t Read(uint32_t p) { return Ptr(p)?R32(p):0; }
struct Loaded {
    uint32_t manager=0, player=0, vehicle=0, cfg=0, custom=0, race=0;
};
Loaded LoadedCar(int slot) {
    Loaded out;out.manager=Read(kPlayerManager);uint32_t count=Read(out.manager+0x28);
    if(!count||count>8||slot<0||uint32_t(slot)>=count)return out;
    out.player=Read(out.manager+8+uint32_t(slot)*4);out.vehicle=Read(out.player+0x14);
    if(out.vehicle && Mapped(out.vehicle,0x6C))out.cfg=Read(out.vehicle+0x64),out.custom=Read(out.vehicle+0x68);
    out.race=Read(Read(0x533E24)+4);return out;
}

// player i's car, phInst and rigid body, or false
bool PlayerChain(int i, Chain& c) {
    uint32_t mgr = Ptr(kPlayerManager) ? R32(kPlayerManager) : 0;
    if (!Ptr(mgr + 0x28) || i >= int(R32(mgr + 0x28)) || !Ptr(mgr + 8 + i * 4)) return false;
    uint32_t player = R32(mgr + 8 + i * 4);
    if (!Ptr(player + 0x14)) return false;
    uint32_t vehicle = R32(player + 0x14);
    if (!Ptr(vehicle + 0x1C)) return false;
    c.car = R32(vehicle + 0x08), c.inst = R32(vehicle + 0x1C);
    if (!Ptr(c.car + 0xB0) || !Ptr(c.inst + 0x40) || !Ptr(c.car + 0x68)) return false;
    uint32_t collider = R32(c.car + 0x68);
    if (!Ptr(collider + 0x10)) return false;
    c.body = R32(collider + 0x10);
    return Ptr(c.body + 0x8C);
}

// partners by setup slot (1..7): their car description, their latest poses
struct Remote {
    bool active = false;
    std::array<uint8_t, kCarCfgBytes> cfg{};
    bool have_cfg = false;
    uint64_t revision = 0, binding_revision = 0, applications = 0;
    uint32_t cfg_sequence = 0; bool have_sequence = false;
    Loaded binding{};
    std::array<uint8_t,kCarCfgBytes> loaded_cfg{};
    bool known_model = false, model_pending = false;
    struct Installed { uint32_t target=0, vtable=0; uint64_t revision=0, binding=0; };
    std::array<Installed,3> installed{}; // current setup, next setup, loaded vehicle
    struct Sample {
        Pose pose;
        uint64_t at;  // the sender's clock, ms
        uint64_t got; // ours, ms
    };
    std::deque<Sample> samples;
    int64_t clock_offset = 0;  // ours - theirs, the smallest seen (the fastest packet)
    bool offset_set = false;
};
std::mutex g_lock;
std::array<Remote, 8> g_remote;
bool g_slots_written = false;

// a new world (a race or the cruise loaded): the local car's grid. Every copy builds the same grid, the local player
// in place 0 and the partners after it, so a partner (grid > 0) takes over the place its own world gave the car of
// slot 'grid' (the host's car there), and the partners' cars are left where the game put them for a moment.
int g_grid = 0;
uint32_t g_world_car = 0;  // the local car object of the world now loaded, 0 none
uint64_t g_world_at = 0;
bool g_grid_done = false;
constexpr uint64_t kSettleMs = 1000;

void Observe(Remote& r,int slot) {
    Loaded live=LoadedCar(slot);
    if(live.manager!=r.binding.manager||live.player!=r.binding.player||live.vehicle!=r.binding.vehicle||
       live.cfg!=r.binding.cfg||live.custom!=r.binding.custom||live.race!=r.binding.race) {
        r.binding=live;r.binding_revision++;r.installed={};r.known_model=false;
    }
    if(!r.known_model&&live.vehicle&&ConfigPtr(live.cfg)&&ValidConfig(static_cast<const uint8_t*>(HostPtr(live.cfg)))) {
        std::memcpy(r.loaded_cfg.data(),HostPtr(live.cfg),kCarCfgBytes);
        if(!live.custom)r.known_model=true; // still constructing; no drawing resource to contradict the config
        else if(Read(live.custom)==0x454ADC && live.custom<=UINT32_MAX-0x106C && Mapped(live.custom,0x106C) &&
                ValidName(static_cast<const uint8_t*>(HostPtr(live.custom+0x102C)))) {
            // F44E0..F4548 retains the construction name independently from CarCfg. Prefer the real model.
            std::memcpy(r.loaded_cfg.data()+kTypeName,HostPtr(live.custom+0x102C),kTypeNameBytes);r.known_model=true;
        }
    }
    r.model_pending = r.have_cfg && live.vehicle && (!r.known_model || !SameResources(r.cfg.data(),r.loaded_cfg.data()));
}

bool Overlaps(uint32_t a,uint32_t b) {
    return ConfigPtr(a)&&ConfigPtr(b)&&uint64_t(a)<uint64_t(b)+kCarCfgBytes&&uint64_t(b)<uint64_t(a)+kCarCfgBytes;
}

void Install(Remote& r,int slot,int target,uint32_t cfg,bool allow_model_change) {
    if(!r.have_cfg||!ConfigPtr(cfg))return;
    // Never overwrite another player's configuration through an alias, including overlapping objects.
    for(int i=0;i<8;i++)if(i!=slot && Overlaps(cfg,LoadedCar(i).cfg))return;
    for(uint32_t global:{kSetupCurrent,kSetupNext}) {
        uint32_t setup=Read(global);
        if(!setup||!Mapped(setup,kSlot+8*kSlotSize))continue;
        for(int i=0;i<8;i++)if(i!=slot && Overlaps(cfg,Read(setup+kSlot+uint32_t(i)*kSlotSize+kSlotCar)))return;
    }
    if(r.binding.vehicle) {
        // A different model must not be paired with the old mesh/body/audio. Only independent NEXT data may
        // be staged; the current/live configuration remains the comparison base for the native next load.
        if(r.model_pending && (!allow_model_change||Overlaps(cfg,r.binding.cfg)))return;
        uint32_t current=Read(kSetupCurrent);
        if(r.model_pending && current && Mapped(current,kSlot+8*kSlotSize) &&
           Overlaps(cfg,Read(current+kSlot+uint32_t(slot)*kSlotSize+kSlotCar)))return;
        if(!r.model_pending && !r.known_model)return;
    }
    uint32_t vtable=R32(cfg);
    for(const auto& prior:r.installed)if(prior.target==cfg&&prior.vtable==vtable&&prior.revision==r.revision&&
        prior.binding==r.binding_revision && ValidConfig(static_cast<const uint8_t*>(HostPtr(cfg))) &&
        SameResources(static_cast<const uint8_t*>(HostPtr(cfg)),r.cfg.data())) { r.installed[size_t(target)]=prior;return; }
    // CopyFrom271E20 preserves the vtable and marks dirtiness locally. Never copy the sender's transient mask.
    uint32_t dirty=R32(cfg+8);
    std::memcpy(HostPtr(cfg+4),r.cfg.data()+4,4);
    std::memcpy(HostPtr(cfg+0x0C),r.cfg.data()+0x0C,kCarCfgBytes-0x0C);
    // 3FFF is CopyFrom's aggregate mask. Bit80 belongs to E0/E1 (271E00), not to a model rebuild request.
    bool full=allow_model_change&&(!r.binding.vehicle||r.model_pending);
    W32(cfg+8,dirty|(full?0x3FFFu:kLiveDirty));
    r.installed[size_t(target)]={cfg,vtable,r.revision,r.binding_revision};r.applications++;
}

void WriteSlot(uint32_t setup, int slot, Remote& r, bool next) {
    uint32_t s = setup + kSlot + uint32_t(slot) * kSlotSize;
    uint32_t cfg = R32(s + kSlotCar);
    // NEXT may share the loaded configuration. Install's alias guard keeps a pending model out of it.
    Install(r,slot,next?1:0,cfg,next||!r.binding.vehicle);
    W32(s + kSlotPad, 0xFFFFFFFF);
    W32(s + kSlotHud, 0xFFFFFFFF);  // no HUD/camera: never request a second viewport
    W32(s + kSlotPlayer, 0xFFFFFFFF);
    G<uint8_t>(s + kSlotOut) = 0;
}

// both setups say as many humans as there are partners with a car, slot 1.. theirs; the local player stays slot 0
void KeepSetups() {
    int humans = 1;
    for (int i = 1; i < 8; i++)
        if (g_remote[i].active && g_remote[i].have_cfg) humans = i + 1;
    for(int i=1;i<8;i++)Observe(g_remote[i],i);
    for (uint32_t global : {kSetupCurrent, kSetupNext}) {
        uint32_t setup = Ptr(global) ? R32(global) : 0;
        if (!setup || !Mapped(setup,0x3E0)) continue;
        if (G<uint8_t>(setup + kSetupNetwork)) continue;  // the game's own multiplayer modes: leave them alone
        int slots = humans;
        // A loaded race owns these players/progress entries. Disconnecting must not change its human count:
        // state 6 otherwise skips the normal result/payment path (0x1C3773..0x1C37A4). Keep both setups
        // equal too: 0x20260 can reload the world when their human counts differ.
        uint32_t holder = Ptr(0x533E24) ? R32(0x533E24) : 0;
        uint32_t race = Ptr(holder + 4) ? R32(holder + 4) : 0;
        if (Ptr(race) && R32(race) != 0x46A7D8) {
            uint32_t current = Ptr(kSetupCurrent) ? R32(kSetupCurrent) : 0;
            int existing = Ptr(current + kSetupHumans) ? int(R32(current + kSetupHumans)) : 0;
            if (existing >= 1 && existing <= 8) slots = existing;
        }
        for (int i = 1; i < slots; i++) WriteSlot(setup, i, g_remote[i],global==kSetupNext);
        if (int(R32(setup + kSetupHumans)) != slots) W32(setup + kSetupHumans, uint32_t(slots));
        if (R32(setup + kSetupViews) != 1) W32(setup + kSetupViews, 1);
    }
    uint32_t current=Read(kSetupCurrent);
    bool native_net=current&&Mapped(current,0x109)&&G<uint8_t>(current+kSetupNetwork);
    for(int i=1;i<8 && !native_net;i++) {
        Remote& r=g_remote[i];
        if(r.active && !r.model_pending && r.binding.vehicle)Install(r,i,2,r.binding.cfg,false);
    }
    g_slots_written = humans > 1;
}

// a pose between two samples, or ahead of the last by its velocity
Pose Blend(const Remote& r, uint64_t now) {
    constexpr int64_t kDelayMs = 100;  // shown this far behind the newest packet: two samples to blend between
    int64_t t = int64_t(now) - r.clock_offset - kDelayMs;
    const auto& s = r.samples;
    for (size_t i = s.size(); i-- > 1;) {
        const auto &a = s[i - 1], &b = s[i];
        if (int64_t(a.at) <= t && t <= int64_t(b.at) && b.at > a.at) {
            float k = float(t - int64_t(a.at)) / float(b.at - a.at);
            Pose p = b.pose;
            for (int r = 0; r < 4; r++)
                for (int c = 0; c < 3; c++) p.m[r][c] = a.pose.m[r][c] + (b.pose.m[r][c] - a.pose.m[r][c]) * k;
            for (int c = 0; c < 3; c++) p.vel[c] = a.pose.vel[c] + (b.pose.vel[c] - a.pose.vel[c]) * k;
            // the rows blended are not quite unit and square: rebuild them from forward and up
            float* up = p.m[1];
            float* fw = p.m[2];
            float l = std::sqrt(fw[0] * fw[0] + fw[1] * fw[1] + fw[2] * fw[2]);
            for (int c = 0; c < 3; c++) fw[c] /= l > 1e-6f ? l : 1.0f;
            float d = up[0] * fw[0] + up[1] * fw[1] + up[2] * fw[2];
            for (int c = 0; c < 3; c++) up[c] -= fw[c] * d;
            l = std::sqrt(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
            for (int c = 0; c < 3; c++) up[c] /= l > 1e-6f ? l : 1.0f;
            float* rt = p.m[0];  // right = up x forward, the handedness the game's rows have
            rt[0] = up[1] * fw[2] - up[2] * fw[1], rt[1] = up[2] * fw[0] - up[0] * fw[2], rt[2] = up[0] * fw[1] - up[1] * fw[0];
            return p;
        }
    }
    Pose p = s.back().pose;  // newer than all: extrapolate a little by the velocity
    float ahead = std::clamp(float(t - int64_t(s.back().at)) / 1000.0f, 0.0f, 0.25f);
    for (int c = 0; c < 3; c++) p.m[3][c] += p.vel[c] * ahead;
    return p;
}

void Apply(const Chain& c, const Pose& p) {
    float lift[3];
    for (int i = 0; i < 3; i++) lift[i] = RF(c.body + 0x80 + i * 4) - RF(c.inst + 0x34 + i * 4);
    if (std::fabs(lift[0]) + std::fabs(lift[1]) + std::fabs(lift[2]) > 3.0f) lift[0] = lift[2] = 0, lift[1] = 0.57f;
    std::memcpy(HostPtr(c.inst + 0x10), p.m, sizeof p.m);
    std::memcpy(HostPtr(c.body + 0x5C), p.m, 36);
    for (int i = 0; i < 3; i++) G<float>(c.body + 0x80 + i * 4) = p.m[3][i] + lift[i];
    std::memcpy(HostPtr(c.body + 0x44), p.vel, 12);
    std::memcpy(HostPtr(c.body + 0x50), p.ang, 12);
    G<float>(c.car + 0x6C) = p.steer, G<float>(c.car + 0x70) = p.throttle;
    G<float>(c.car + 0x74) = p.brake, G<float>(c.car + 0x78) = p.handbrake;
}

}  // namespace

bool InPlay() {
    Chain c;
    return PlayerChain(0, c);
}

int City() {
    uint32_t career = Ptr(kCareer) ? R32(kCareer) : 0;
    if (!Ptr(career + 0x0C)) return -1;
    uint32_t data = R32(career + 0x0C);
    return Ptr(data + 0x50) ? int(R32(data + 0x50)) : -1;
}

bool Local(Pose& out) {
    Chain c;
    if (!PlayerChain(0, c)) return false;
    std::memcpy(out.m, HostPtr(c.inst + 0x10), sizeof out.m);
    std::memcpy(out.vel, HostPtr(c.body + 0x44), sizeof out.vel);
    std::memcpy(out.ang, HostPtr(c.body + 0x50), sizeof out.ang);
    out.steer = RF(c.car + 0x6C), out.throttle = RF(c.car + 0x70), out.brake = RF(c.car + 0x74), out.handbrake = RF(c.car + 0x78);
    return true;
}

bool LocalCarCfg(uint8_t out[kCarCfgBytes]) {
    if (!Ptr(kGarageCount)) return false;
    uint8_t count = G<uint8_t>(kGarageCount), selected = G<uint8_t>(kGarageSelected);
    if (!count || selected >= 30) return false;
    uint32_t cfg = kGarage + uint32_t(selected) * kCarCfgBytes;
    if (!Mapped(cfg, kCarCfgBytes)) return false;
    std::memcpy(out, HostPtr(cfg), kCarCfgBytes);
    return ValidConfig(out);
}

void Place(const Pose& p) {
    Chain c;
    if (!PlayerChain(0, c)) return;
    Pose stopped = p;
    std::memset(stopped.vel, 0, sizeof stopped.vel);
    std::memset(stopped.ang, 0, sizeof stopped.ang);
    stopped.throttle = stopped.brake = stopped.steer = stopped.handbrake = 0;
    Apply(c, stopped);
    Log("car: placed at %.1f %.1f %.1f", double(p.m[3][0]), double(p.m[3][1]), double(p.m[3][2]));
}

static void StoreConfig(int slot, const uint8_t cfg[kCarCfgBytes], bool ordered, uint32_t sequence) {
    if (slot < 1 || slot > 7 || !ValidConfig(cfg)) return;
    std::lock_guard lock(g_lock);
    Remote& r = g_remote[slot];
    if(ordered) {
        if(r.have_sequence&&int32_t(sequence-r.cfg_sequence)<=0)return;
        r.have_sequence=true;r.cfg_sequence=sequence;
    }
    bool changed = !r.have_cfg || !SameConfig(r.cfg.data(),cfg);
    if(changed) {
        std::memcpy(r.cfg.data(), cfg, kCarCfgBytes);
        std::memset(r.cfg.data()+8,0,4); // foreign dirty bits are consumed on the other PC
        if(!++r.revision)++r.revision;
    }
    r.have_cfg = r.active = true;
    if (changed) Log("car: slot %d partner car %.22s", slot, reinterpret_cast<const char*>(cfg + 0xE3));
}

void SetPartnerCar(int slot, const uint8_t cfg[kCarCfgBytes]) { StoreConfig(slot,cfg,false,0); }
void OnPartnerCar(int slot, const uint8_t cfg[kCarCfgBytes], uint32_t sequence) { StoreConfig(slot,cfg,true,sequence); }

std::string Describe() {
    std::lock_guard lock(g_lock);
    int active=0,pending=0;uint64_t applications=0;
    for(int i=1;i<8;i++)if(g_remote[i].active&&g_remote[i].have_cfg) {
        active++;pending+=g_remote[i].model_pending;applications+=g_remote[i].applications;
    }
    char text[160];std::snprintf(text,sizeof text,"remote_cfg=%d car_model_pending=%d cfg_applied=%llu",active,pending,
                                (unsigned long long)applications);return text;
}

void Drive(int slot, const Pose& p, uint64_t at_ms) {
    if (slot < 1 || slot > 7) return;
    std::lock_guard lock(g_lock);
    Remote& r = g_remote[slot];
    r.active = true;
    uint64_t now = GetTickCount64();
    int64_t offset = int64_t(now) - int64_t(at_ms);
    if (!r.offset_set || offset < r.clock_offset) r.clock_offset = offset, r.offset_set = true;
    if (!r.samples.empty() && at_ms <= r.samples.back().at) return;  // late or repeated
    r.samples.push_back({p, at_ms, now});
    while (r.samples.size() > 32) r.samples.pop_front();
}

void Remove(int slot) {
    if (slot < 1 || slot > 7) return;
    std::lock_guard lock(g_lock);
    if (g_remote[slot].active) Log("car: slot %d partner gone", slot);
    g_remote[slot] = Remote{};
}

int PartnerSlots() {
    std::lock_guard lock(g_lock);
    int n = 0;
    for (int i = 1; i < 8; i++) n += g_remote[i].active && g_remote[i].have_cfg;
    return n;
}

void SetGrid(int grid) {
    std::lock_guard lock(g_lock);
    g_grid = std::clamp(grid, 0, 7);
}

void Tick(uint64_t) {
    uint64_t now = GetTickCount64();  // the clock Drive keeps the samples in
    std::lock_guard lock(g_lock);
    KeepSetups();
    Chain local;
    uint32_t car = PlayerChain(0, local) ? local.car : 0;
    if (car != g_world_car) {
        g_world_car = car, g_world_at = now, g_grid_done = false;
        if (car) Log("car: new world, local car %08X, grid %d", car, g_grid);
    }
    if (!car || now - g_world_at < kSettleMs) return;
    if (!g_grid_done) {
        g_grid_done = true;
        Chain place;
        if (g_grid > 0 && PlayerChain(g_grid, place)) {  // the start this world gave the car of our grid place
            Pose start;
            std::memcpy(start.m, HostPtr(place.inst + 0x10), sizeof start.m);
            Apply(local, start);
            Log("car: took grid place %d at %.1f %.1f %.1f", g_grid, double(start.m[3][0]), double(start.m[3][1]),
                double(start.m[3][2]));
        }
    }
    for (int i = 1; i < 8; i++) {
        Remote& r = g_remote[i];
        if (!r.active || r.samples.empty()) continue;
        if (now - r.samples.back().got > 3000) continue;  // not heard of late: its car stays where it was
        Chain c;
        if (!PlayerChain(i, c)) continue;
        Apply(c, Blend(r, now));
    }
}

}  // namespace mod::car
