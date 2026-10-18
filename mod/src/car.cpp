// The local car (read, and placed for "take me to my partner"); the partner's car comes next (see car.h)
#include "car.h"

#include <cstring>

#include "mod.h"

namespace mod::car {

namespace {

constexpr uint32_t kPlayerManager = 0x558814;
constexpr uint32_t kCareer = 0x557F0C;  // mcPgCareer*, +0x0C career data (+0x50 current city)

bool Ptr(uint32_t va) { return va >= 0x10000 && Mapped(va, 4); }

// the local player's vehicle, car, phInst and rigid body, or false
bool Chain(uint32_t& car, uint32_t& inst, uint32_t& body) {
    uint32_t mgr = Ptr(kPlayerManager) ? R32(kPlayerManager) : 0;
    if (!Ptr(mgr) || !Ptr(mgr + 8)) return false;
    uint32_t player = R32(mgr + 8);
    if (!Ptr(player + 0x14)) return false;
    uint32_t vehicle = R32(player + 0x14);
    if (!Ptr(vehicle + 0x1C)) return false;
    car = R32(vehicle + 0x08), inst = R32(vehicle + 0x1C);
    if (!Ptr(car + 0xB0) || !Ptr(inst + 0x40) || !Ptr(car + 0x68)) return false;
    uint32_t collider = R32(car + 0x68);
    if (!Ptr(collider + 0x10)) return false;
    body = R32(collider + 0x10);
    return Ptr(body + 0x8C);
}

}  // namespace

bool InPlay() {
    uint32_t car, inst, body;
    return Chain(car, inst, body);
}

int City() {
    uint32_t career = Ptr(kCareer) ? R32(kCareer) : 0;
    if (!Ptr(career + 0x0C)) return -1;
    uint32_t data = R32(career + 0x0C);
    return Ptr(data + 0x50) ? int(R32(data + 0x50)) : -1;
}

bool Local(Pose& out) {
    uint32_t car, inst, body;
    if (!Chain(car, inst, body)) return false;
    std::memcpy(out.m, HostPtr(inst + 0x10), sizeof out.m);
    std::memcpy(out.vel, HostPtr(body + 0x44), sizeof out.vel);
    std::memcpy(out.ang, HostPtr(body + 0x50), sizeof out.ang);
    out.steer = RF(car + 0x6C), out.throttle = RF(car + 0x70), out.brake = RF(car + 0x74), out.handbrake = RF(car + 0x78);
    return true;
}

void Place(const Pose& p) {
    uint32_t car, inst, body;
    if (!Chain(car, inst, body)) return;
    // the body's position is its centre of mass, a little above the instance's: keep that offset
    float lift[3];
    for (int i = 0; i < 3; i++) lift[i] = RF(body + 0x80 + i * 4) - RF(inst + 0x34 + i * 4);
    std::memcpy(HostPtr(inst + 0x10), p.m, sizeof p.m);
    std::memcpy(HostPtr(body + 0x5C), p.m, 36);
    for (int i = 0; i < 3; i++) G<float>(body + 0x80 + i * 4) = p.m[3][i] + lift[i];
    std::memset(HostPtr(body + 0x44), 0, 24);  // stopped: no velocity, no spin
    Log("car: placed at %.1f %.1f %.1f", double(p.m[3][0]), double(p.m[3][1]), double(p.m[3][2]));
}

void Drive(int, const Pose&, uint64_t) {}
void Remove(int) {}
void Tick(uint64_t) {}

}  // namespace mod::car
