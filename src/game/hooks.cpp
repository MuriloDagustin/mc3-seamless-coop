// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include "game/control.h"
#include "game/coop.h"
#include "game/mc3.h"
#include "kernel/kernel.h"

// config/game.toml [[hook]]s

// InitWinSock: the WSADATA at [esp+0x0C] reports the 2.0 the game asked for (the SDK says 2.2)
void Mc3WinsockVersion(X86Context& ctx, uint8_t*) {
    kernel::W16(ctx.esp + 0x0C, 0x0002);
}

namespace mc3 {

void Loaded() {
    apu::Init();
}

}  // namespace mc3

// The game's present (its one call of D3DDevice_Swap, 0x2D2E0F), on the game thread once a frame from the main
// loop and the load screens alike: the frame tick for the test channel and the co-op.
X86_EXTERN_FUNC(__imp__sub_002D2990);
X86_FUNC(sub_002D2990) {
    control::Tick(ctx);
    coop::Frame(ctx);
    __imp__sub_002D2990(ctx, base);
}
