// Experimental pose authority for common career races, see docs/RE-IA-IMPLEMENTACAO.md.
// car+15C bit10 blocks Opp::Update in race states5/6 for vehCar vtable4564F0. Input::Update still runs.
#include "ai.h"
#include "car.h"
#include "mod.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace mod::ai {
namespace {
constexpr uint32_t kMagic = 0x3149414D, kCarVt = 0x4564F0, kBrainMask = 0x10;
constexpr int kMax = 8;
constexpr uint64_t kSendMs = 50, kStaleMs = 500;
// One opponent per datagram; the receiver only publishes a complete cycle. No guest pointers on the wire.
#pragma pack(push, 1)
struct WireHeader {
    uint32_t magic, serial, layout, cycle;
    int16_t city, race_idx;
    uint8_t count, slot, version, reserved;
};
struct WireCar {
    car::Pose pose;
    float body_position[3]; // centre of mass differs from the visible origin
    // Progress imports are opt-in; a complete native result still requires human progress/phase control.
    uint8_t finished, dnf; int16_t checkpoint;
    int32_t laps, place;
    float finish_time, progress;
    uint8_t arrested, participation, checkpoints, checkpoint_aux;
    float timer, last_lap_at, last_lap_time, best_lap_time, last_checkpoint_at, distance_squared;
    int32_t score;
    uint32_t checkpoint_mask[4];
};
#pragma pack(pop)
struct Chain { uint32_t opp=0, vehicle=0, car=0, inst=0, coll=0, body=0, matrix=0, progress=0; };
struct Pool { uint32_t mgr=0, array=0, race=0, layout=0; int count=0; std::array<Chain,kMax> cars{}; };
struct Saved { Chain chain{}; uint32_t mgr=0, array=0, race=0, original=0; bool owned=false; };
struct Batch { WireHeader head{}; std::array<WireCar,kMax> cars{}; uint32_t mask=0; uint64_t at=0; };
std::mutex g_mutex;
uint32_t g_serial=0, g_cycle=0; int g_city=-1, g_race_idx=-1;
uint64_t g_sent=0;
Batch g_pending{}, g_ready{};
std::array<Saved,kMax> g_saved{};
int g_applied=0; const char* g_reason="off";
bool g_sync_progress=false;

bool Valid(uint32_t p, uint32_t bytes=4) { return p>=0x10000 && bytes && p<=0xFFFFFFFFu-bytes && Mapped(p,bytes); }
uint32_t Read(uint32_t p) { return Valid(p) ? R32(p) : 0; }
void Hash(uint32_t& h, uint32_t x) { h=(h^x)*16777619u; }
bool HashString(uint32_t& h, uint32_t p, unsigned limit) {
    if (!p) return false;
    for(unsigned i=0;i<limit;i++) { if(!Valid(p+i,1))return false; uint8_t c=G<uint8_t>(p+i);Hash(h,c);if(!c)return i>0; }
    return false; // truncated/unknown identity cannot be matched safely
}
bool Common(uint32_t vt) { return vt==0x46B2D8 || vt==0x46B4B8 || vt==0x46ABD8 || vt==0x46B188; }
bool Playing(bool source=false) {
    uint32_t machine=Read(0x557EEC), game=Read(0x557ED0);
    uint32_t state=Read(machine+4);
    return Valid(machine+4) && state>=5 && state<=(source?10u:6u) && Valid(game+4) && Read(game+4)!=3;
}
bool ReadPool(Pool& out,bool source=false) {
    if(!g_base || !g_serial || !Playing(source)) return false;
    Pool p; p.race=Read(Read(0x533E24)+4);p.mgr=Read(0x534074);
    if(!Valid(p.race,0x125C) || !Common(Read(p.race)) || !Valid(p.mgr,12))return false;
    uint32_t n=Read(p.mgr), nc=Read(p.mgr+4);p.array=Read(p.mgr+8);
    if(!nc || nc>kMax || n<nc || n>128 || !Valid(p.array,n*0x43C))return false;
    uint32_t descriptors=Read(p.race+0x2C), progress=Read(p.race+0x658);
    if(!Valid(descriptors,nc*0x88) || !Valid(progress,nc*0x60))return false;
    uint32_t h=2166136261u;Hash(h,Read(p.race+0x10));Hash(h,Read(p.race+0x24));Hash(h,Read(p.race+0x6C8));Hash(h,nc);
    for(unsigned i=0;i<nc;i++) {
        Chain c;c.opp=p.array+i*0x43C;c.progress=progress+i*0x60;
        uint32_t d=descriptors+i*0x88;
        if(G<uint16_t>(c.opp+0x54)!=i || !G<uint8_t>(d+9))return false;
        c.vehicle=Read(c.opp+0x20);if(!Valid(c.vehicle,0x20))return false;
        c.car=Read(c.vehicle+8);c.inst=Read(c.vehicle+0x1C);
        if(!Valid(c.car,0x160) || Read(c.car)!=kCarVt || !Valid(c.inst,0x40))return false;
        c.coll=Read(c.car+0x68);if(!Valid(c.coll,0x18))return false;
        c.body=Read(c.coll+0x10);c.matrix=Read(c.coll+0x14);
        if(!Valid(c.body,0x8C) || !Valid(c.matrix,0x30))return false;
        // Do not overwrite a body pose with a different visible-origin pose when these matrices alias.
        if(c.matrix==c.body+0x5C)return false;
        uint32_t cfg=Read(d+0x64);
        // Driver names are career-local cosmetics. Slot, car model, type and spawn identify the opponent.
        if(!Valid(cfg,car::kCarCfgBytes) || !HashString(h,cfg+0xE3,car::kCarCfgBytes-0xE3))return false;
        Hash(h,i);Hash(h,G<uint8_t>(d));Hash(h,G<uint8_t>(d+9));Hash(h,Read(c.opp+0x68));
        // Spawn coordinates and route label, not process-local descriptor/config/driver pointers.
        for(unsigned j=0;j<4;j++)Hash(h,Read(d+0x38+j*4));
        p.cars[i]=c;
    }
    p.count=int(nc);p.layout=h;out=p;return true;
}
bool Finite(const WireCar& c) {
    // Packed floats are copied out, avoiding unaligned float pointers.
    std::array<float,sizeof(car::Pose)/sizeof(float)> values{};
    std::memcpy(values.data(),&c.pose,sizeof(c.pose));
    for(float f:values)if(!std::isfinite(f))return false;
    for(float f:c.body_position)if(!std::isfinite(f)||std::fabs(f)>1000000.f)return false;
    for(int i=0;i<3;i++) {
        float len=0;for(int j=0;j<3;j++)len+=c.pose.m[i][j]*c.pose.m[i][j];
        if(len<0.5f || len>1.5f || std::fabs(c.pose.m[3][i])>1000000.f ||
           std::fabs(c.pose.vel[i])>2000.f || std::fabs(c.pose.ang[i])>1000.f)return false;
    }
    return c.finished<=1 && c.dnf<=1 && c.arrested<=1 && c.participation<=1 && c.checkpoints<=128 &&
           c.checkpoint>=-1 && c.checkpoint<128 && c.laps>=-1 && c.laps<=128 && c.place>=0 && c.place<=16 &&
           std::isfinite(c.timer) && std::isfinite(c.last_lap_at) && std::isfinite(c.last_lap_time) &&
           std::isfinite(c.best_lap_time) && std::isfinite(c.last_checkpoint_at) && std::isfinite(c.distance_squared) &&
           std::isfinite(c.finish_time) && std::isfinite(c.progress) &&
           c.pose.steer>=-2 && c.pose.steer<=2 && c.pose.throttle>=-1 && c.pose.throttle<=2 &&
           c.pose.brake>=-1 && c.pose.brake<=2 && c.pose.handbrake>=-1 && c.pose.handbrake<=2;
}
void Release() {
    for(auto& s:g_saved)if(s.owned) {
        const Chain& c=s.chain;
        // The old allocation must still be the same registered opponent. Never dereference saved freed memory.
        if(g_base && Read(0x534074)==s.mgr && Read(Read(0x533E24)+4)==s.race &&
           Read(s.mgr+8)==s.array && Valid(c.opp,0x58) && Read(c.opp+0x20)==c.vehicle &&
           Read(c.vehicle+8)==c.car && Valid(c.car,0x160) && Read(c.car)==kCarVt) {
            uint32_t now=Read(c.car+0x15C);
            // Restore our bit only. Native crash/finish flags acquired while puppeted remain intact.
            W32(c.car+0x15C,(now&~kBrainMask)|(s.original&kBrainMask));
        }
        s={};
    }
    g_applied=0;
}
void Apply(const Chain& c, const WireCar& w, float ahead) {
    car::Pose pose=w.pose;
    for(int j=0;j<3;j++)pose.m[3][j]+=pose.vel[j]*ahead;
    std::memcpy(HostPtr(c.inst+0x10),pose.m,48);
    std::memcpy(HostPtr(c.matrix),pose.m,48);
    std::memcpy(HostPtr(c.body+0x5C),pose.m,36);
    for(int j=0;j<3;j++)G<float>(c.body+0x80+j*4)=w.body_position[j]+pose.vel[j]*ahead;
    std::memcpy(HostPtr(c.body+0x44),pose.vel,12);std::memcpy(HostPtr(c.body+0x50),pose.ang,12);
    float mass=RF(c.body+4);
    for(int j=0;j<3;j++)G<float>(c.body+0x2C+j*4)=mass*pose.vel[j];
    // Setter343300: row-vector convention, L = (omega * R^T) * diag(I) * R.
    float local[3]{};
    for(int i=0;i<3;i++)for(int j=0;j<3;j++)local[i]+=pose.ang[j]*pose.m[i][j];
    for(int j=0;j<3;j++) { float l=0;for(int i=0;i<3;i++)l+=local[i]*RF(c.body+0x0C+i*4)*pose.m[i][j];G<float>(c.body+0x38+j*4)=l; }
    float speed=0;for(float v:pose.vel)speed+=v*v;G<float>(c.car+0xB0)=std::sqrt(speed);
    G<float>(c.car+0x6C)=pose.steer;G<float>(c.car+0x70)=pose.throttle;
    G<float>(c.car+0x74)=pose.brake;G<float>(c.car+0x78)=pose.handbrake;
    // Progress is separately opt-in: native checkpoint/ranking handlers can still run after this hook.
}
} // namespace

void SetRace(uint32_t serial,int city,int race_idx,bool sync_progress) {
    std::lock_guard lock(g_mutex);
    if(serial==g_serial && city==g_city && race_idx==g_race_idx && sync_progress==g_sync_progress)return;
    g_serial=(city>=0&&city<4&&race_idx>=0&&race_idx<160)?serial:0;
    g_city=city;g_race_idx=race_idx;g_sync_progress=sync_progress;g_pending={};g_ready={};g_sent=0;g_reason="waiting";
    // Preserve g_saved until Tick can safely restore it on the game thread.
}
bool Snapshot(std::vector<std::vector<uint8_t>>& packets,uint64_t now) {
    std::lock_guard lock(g_mutex);Pool p;
    if(now<g_sent || now-g_sent<kSendMs || !ReadPool(p,true))return false;
    std::array<WireCar,kMax> cars{};
    for(int i=0;i<p.count;i++) {
        const Chain& c=p.cars[i];auto& w=cars[i];
        std::memcpy(w.pose.m,HostPtr(c.inst+0x10),48);std::memcpy(w.pose.vel,HostPtr(c.body+0x44),12);
        std::memcpy(w.pose.ang,HostPtr(c.body+0x50),12);std::memcpy(w.body_position,HostPtr(c.body+0x80),12);
        w.pose.steer=RF(c.car+0x6C);w.pose.throttle=RF(c.car+0x70);w.pose.brake=RF(c.car+0x74);w.pose.handbrake=RF(c.car+0x78);
        w.finished=G<uint8_t>(c.progress+4);w.dnf=G<uint8_t>(c.progress+6);w.checkpoint=G<int16_t>(c.progress+0x34);
        w.laps=int32_t(Read(c.progress+0x18));w.place=int32_t(Read(c.progress+0x14));
        w.finish_time=RF(c.progress+0x10);w.progress=RF(c.progress+0x3C);
        w.arrested=G<uint8_t>(c.progress+5);w.participation=G<uint8_t>(c.progress+7);
        w.checkpoints=G<uint8_t>(c.progress+0x48);w.checkpoint_aux=G<uint8_t>(c.progress+0x49);
        w.timer=RF(c.progress+0x0C);w.last_lap_at=RF(c.progress+0x1C);w.last_lap_time=RF(c.progress+0x24);
        w.best_lap_time=RF(c.progress+0x28);w.last_checkpoint_at=RF(c.progress+0x30);w.distance_squared=RF(c.progress+0x38);
        w.score=int32_t(Read(c.progress+0x44));std::memcpy(w.checkpoint_mask,HostPtr(c.progress+0x4C),16);
        if(!Finite(w))return false;
    }
    if(Read(0x534074)!=p.mgr || Read(p.mgr+8)!=p.array || Read(Read(0x533E24)+4)!=p.race || !Playing(true))return false;
    g_sent=now;uint32_t cycle=++g_cycle;packets.clear();
    for(int i=0;i<p.count;i++) {
        uint8_t terminal=Read(Read(0x557EEC)+4)>=7?1:0;
        WireHeader h{kMagic,g_serial,p.layout,cycle,int16_t(g_city),int16_t(g_race_idx),uint8_t(p.count),uint8_t(i),2,terminal};
        std::vector<uint8_t> out(sizeof(h)+sizeof(WireCar));
        std::memcpy(out.data(),&h,sizeof(h));std::memcpy(out.data()+sizeof(h),&cars[i],sizeof(WireCar));packets.push_back(std::move(out));
    }
    return true;
}
void Receive(const uint8_t* data,int bytes) {
    if(!data || bytes!=int(sizeof(WireHeader)+sizeof(WireCar)))return;
    WireHeader h;WireCar c;std::memcpy(&h,data,sizeof(h));std::memcpy(&c,data+sizeof(h),sizeof(c));
    if(h.magic!=kMagic || h.version!=2 || (h.reserved&~1u) || !h.serial || !h.count || h.count>kMax || h.slot>=h.count || !Finite(c))return;
    std::lock_guard lock(g_mutex);
    if(h.serial!=g_serial || h.city!=g_city || h.race_idx!=g_race_idx)return;
    if(g_ready.mask && int32_t(h.cycle-g_ready.head.cycle)<=0)return;
    if(g_pending.mask && int32_t(h.cycle-g_pending.head.cycle)<0)return;
    if(!g_pending.mask || h.cycle!=g_pending.head.cycle)g_pending={h,{},0,0};
    if(h.layout!=g_pending.head.layout || h.count!=g_pending.head.count || h.reserved!=g_pending.head.reserved)return;
    g_pending.cars[h.slot]=c;g_pending.mask|=1u<<h.slot;
    if(g_pending.mask==((1u<<h.count)-1)) { g_pending.at=GetTickCount64();g_ready=g_pending;g_pending={}; }
}
void Tick(bool partner,uint64_t now) {
    std::lock_guard lock(g_mutex);Pool p;
    if(!partner || !g_serial || !g_ready.mask) { Release();g_reason="off";return; }
    // Retain the host's final snapshot while this player is still racing; a later race or disconnect releases it.
    if(now<g_ready.at || (!(g_ready.head.reserved&1) && now-g_ready.at>=kStaleMs)) { Release();g_reason="stale";return; }
    if(!ReadPool(p,(g_ready.head.reserved&1)!=0)) { Release();g_reason="waiting_pool";return; }
    if(p.layout!=g_ready.head.layout || p.count!=g_ready.head.count) { Release();g_reason="layout_mismatch";return; }
    if(g_sync_progress && (Read(p.race+0x24)==0 || Read(p.race+0x24)>128)) { Release();g_reason="unsupported_checkpoints";return; }
    for(int i=0;i<p.count;i++) {
        const Chain& c=p.cars[i];
        const WireCar& incoming=g_ready.cars[i];
        uint32_t checkpoints=Read(p.race+0x24);
        if(g_sync_progress && (incoming.checkpoints>checkpoints || incoming.checkpoint_aux>checkpoints ||
           (incoming.checkpoint>=0 && uint32_t(incoming.checkpoint)>=checkpoints))) {
            Release();g_reason="invalid_progress";return;
        }
        if(!std::isfinite(RF(c.body+4)) || RF(c.body+4)<=0 || RF(c.body+4)>10000000.f) { Release();g_reason="invalid_body";return; }
        for(int j=0;j<3;j++)if(!std::isfinite(RF(c.body+0x0C+j*4)) || RF(c.body+0x0C+j*4)<=0) { Release();g_reason="invalid_body";return; }
    }
    if(g_saved[0].owned && (g_saved[0].mgr!=p.mgr || g_saved[0].array!=p.array || g_saved[0].race!=p.race || g_saved[0].chain.car!=p.cars[0].car))Release();
    for(int i=0;i<p.count;i++) {
        const Chain& c=p.cars[i];auto& saved=g_saved[i];
        if(saved.owned && (saved.chain.car!=c.car || saved.chain.vehicle!=c.vehicle)) { Release();g_reason="changed_vehicle";return; }
    }
    for(int i=0;i<p.count;i++) {
        const Chain& c=p.cars[i];auto& saved=g_saved[i];
        if(!saved.owned)saved={c,p.mgr,p.array,p.race,Read(c.car+0x15C),true};
        W32(c.car+0x15C,Read(c.car+0x15C)|kBrainMask);
        Apply(c,g_ready.cars[i],g_ready.head.reserved&1?0.f:std::min(float(now-g_ready.at)/1000.f,0.10f));
        if(g_sync_progress) {
            const WireCar& w=g_ready.cars[i];uint32_t prog=c.progress;
            G<uint8_t>(prog+4)=w.finished;G<uint8_t>(prog+5)=w.arrested;G<uint8_t>(prog+6)=w.dnf;
            G<uint8_t>(prog+7)=w.participation;G<float>(prog+0x0C)=w.timer;G<float>(prog+0x10)=w.finish_time;
            W32(prog+0x14,uint32_t(w.place));W32(prog+0x18,uint32_t(w.laps));G<float>(prog+0x1C)=w.last_lap_at;
            G<float>(prog+0x24)=w.last_lap_time;G<float>(prog+0x28)=w.best_lap_time;G<float>(prog+0x30)=w.last_checkpoint_at;
            G<int16_t>(prog+0x34)=w.checkpoint;G<float>(prog+0x38)=w.distance_squared;G<float>(prog+0x3C)=w.progress;
            W32(prog+0x44,uint32_t(w.score));G<uint8_t>(prog+0x48)=w.checkpoints;G<uint8_t>(prog+0x49)=w.checkpoint_aux;
            std::memcpy(HostPtr(prog+0x4C),w.checkpoint_mask,16);
        }
    }
    if(g_sync_progress) {
        uint32_t finished=0;for(int i=0;i<p.count;i++)finished+=g_ready.cars[i].finished!=0;
        W32(p.race+0x1254,finished); // assign from complete authority, never increment again for repeated packets
    }
    g_applied=p.count;g_reason="experimental";
}
void Stop() { std::lock_guard lock(g_mutex);g_serial=0;g_sync_progress=false;g_pending={};g_ready={};g_reason="stopping"; }
std::string Describe() {
    std::lock_guard lock(g_mutex);char out[192];std::snprintf(out,sizeof(out),"ai=%s applied=%d serial=%u layout=%08X cycle=%u received=%u",g_reason,g_applied,g_serial,g_ready.head.layout,g_ready.head.cycle,g_ready.head.count);return out;
}
} // namespace mod::ai
