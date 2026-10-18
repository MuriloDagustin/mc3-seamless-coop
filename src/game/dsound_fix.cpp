// DirectSound wrappers xbe_symbols leaves unnamed in MC3. The SDK's buffers and streams are host
// objects, so the original library bodies of these setters would read a guest voice that does not
// exist. They are (this, value) setters with no output the game reads, by their place in the XDK 5849
// IDirectSoundBuffer order and the voice method each one reaches (see docs/DIAGNOSTICO.md):
//   buffer 3C2090 SetPitch, 3C20AC SetLFO, 3C20C8 SetEG, 3C2240/3C225C/3C2278 (voice 3C0C81/0C94/0DC5)
//   stream 3C2299 SetPitch, 3C229E SetLFO, 3C22D4/3C22D9/3C22DE (the same three voice methods)
// The software mixer has no pitch, LFO or envelope, so S_OK is what the game needs.
#include "kernel/kernel.h"
#include "xdk/hle.h"

#define DS_NOOP(addr)                              \
    X86_FUNC(sub_##addr) { kernel::Return(ctx, 0, 2); }

DS_NOOP(003C2090)
DS_NOOP(003C20AC)
DS_NOOP(003C20C8)
DS_NOOP(003C2240)
DS_NOOP(003C225C)
DS_NOOP(003C2278)
DS_NOOP(003C2299)
DS_NOOP(003C229E)
DS_NOOP(003C22D4)
DS_NOOP(003C22D9)
DS_NOOP(003C22DE)

// IDirectSound_CreateSoundStream(this, desc, stream out, outer), ret 0x10: XACT's streams (sub_0040F050). The
// library's CDirectSound::CreateSoundStream (3C3511) builds a hardware voice; the SDK's DirectSoundCreateStream
// (3C391A, its host version) makes the same stream as a host object.
X86_FUNC(sub_003C36E9) {
    uint32_t desc = kernel::R32(ctx.esp + 8), out = kernel::R32(ctx.esp + 12);
    uint32_t result = kernel::CallGuest(ctx, 0x3C391A, {desc, out});
    kernel::Return(ctx, result, 4);
}
