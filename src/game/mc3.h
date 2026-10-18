// Midnight Club 3 hooks shared between the game sources
#pragma once
#include <cstdint>

namespace mc3 {

void Loaded();  // image in memory, before the entry point

namespace apu {
void Init();
}

}  // namespace mc3
