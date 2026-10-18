// Midnight Club 3's cars for the co-op (car.h). Placeholder until the player's car is mapped in memory:
// no car is reported in play, so the session runs and shows its partners in menus.
#include "game/car.h"

namespace mc3::car {

bool InPlay() { return false; }
int City() { return -1; }
bool Local(Pose&) { return false; }
void Place(const Pose&) {}
void Drive(int, const Pose&, uint64_t) {}
void Remove(int) {}
void Tick(X86Context&, uint64_t) {}

}  // namespace mc3::car
