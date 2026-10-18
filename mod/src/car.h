#pragma once
// Midnight Club 3's cars in the runner's guest memory (docs/RE-MAPA.txt, checked live):
// player i = [[0x558814] + 0x08 + 4i], vehicle = [player + 0x14], car = [vehicle + 0x08], phInst = [vehicle + 0x1C]
// (Matrix34 at +0x10: rows 0..2 then the position at +0x34), body = [[car + 0x68] + 0x10] (velocity +0x44, spin
// +0x50, matrix +0x5C with the position at +0x80), speed car+0xB0, controls car+0x6C steer, +0x70 throttle,
// +0x74 brake, +0x78 handbrake. The car faces -row2.
#include <cstdint>

namespace mod::car {

// a car's place and motion, world space; the matrix rows as the game stores them, then the position
#pragma pack(push, 1)
struct Pose {
    float m[4][3]{};
    float vel[3]{}, ang[3]{};
    float steer = 0, throttle = 0, brake = 0, handbrake = 0;
};
#pragma pack(pop)

bool InPlay();             // a player car exists (cruise or a race)
int City();                // the loaded city, -1 none
bool Local(Pose& out);     // the local player's car now
void Place(const Pose& p); // the local car moved there, stopped ("take me to my partner")

// a partner's car in this world, from its latest pose and the time it was sent
void Drive(int player, const Pose& p, uint64_t at_ms);
void Remove(int player);
void Tick(uint64_t now_ms);  // the mod's tick: puppets stepped toward their targets

}  // namespace mod::car
