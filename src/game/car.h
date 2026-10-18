#pragma once
// Midnight Club 3's cars as the co-op sees them: the local player's car read each frame, a partner's car shown
// by a car of the game's own driven from the network. Game thread only.
#include <cstdint>

#include "kernel/kernel.h"

namespace mc3::car {

// a car's place and motion, world space; the matrix rows are right, up, forward, position
struct Pose {
    float m[4][3]{};
    float vel[3]{}, ang[3]{};
    float steer = 0, throttle = 0, brake = 0;
};

bool InPlay();             // the player drives in the city (not a menu, a load or the garage)
int City();                // the loaded city, -1 none
bool Local(Pose& out);     // the player's car this frame
void Place(const Pose& p); // the player's car moved there, stopped (the "take me to my partner" escape)

// a partner's car in this world, from its latest pose and the time it was sent
void Drive(int player, const Pose& p, uint64_t at_ms);
void Remove(int player);   // gone, or not in this city
void Tick(X86Context& ctx, uint64_t now_ms);  // puppets stepped toward their targets, once a frame

}  // namespace mc3::car
