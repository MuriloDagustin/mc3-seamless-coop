#pragma once
// Experimental native bridge probe (docs/RE-PONTE-NATIVA-ABI.md). Disabled unless MC3COOP_NATIVE_PROBE=1.
// Runs only on the game thread hook, refuses unknown builds/contexts, and never touches the user's install.
#include <string>

namespace mod::native {

void Init();       // read the opt-in and the build identity once
bool Enabled();
bool SwapEnabled();  // MC3COOP_CAR_SWAP=1: enable the remote hot-swap transaction
void Tick();       // game-thread hook: one harmless guest call when the world is loaded
bool SwapRemote(int slot);  // game-thread transaction; false keeps the old vehicle
std::string Describe();

}  // namespace mod::native
