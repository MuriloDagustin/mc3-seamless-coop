// Direct3D calls MC3 makes that the SDK's D3D8 has no host version of.
#include <cstring>
#include <mutex>
#include <vector>

#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

using namespace kernel;

namespace {
constexpr uint32_t kDrawVerticesUP = 0x417570;  // D3DDevice_DrawVerticesUP, the SDK's host draw
}

// D3DDevice_DrawIndexedVerticesUP(type, index count, 16 bit indices, vertices, stride), ret 0x14 (the HUD and
// the 2D draws of sub_002D6A18). The library writes the indices into the push buffer behind state the host
// device does not keep; the same primitive drawn from the vertices in index order is what it shows.
X86_FUNC(sub_004176E0) {
    uint32_t type = R32(ctx.esp + 4), count = R32(ctx.esp + 8), indices = R32(ctx.esp + 12);
    uint32_t vertices = R32(ctx.esp + 16), stride = R32(ctx.esp + 20);
    static std::mutex lock;
    static uint32_t scratch = 0, scratch_size = 0;
    if (count && stride && stride <= 256 && indices && vertices) {
        std::lock_guard guard(lock);  // the game draws from one thread; held for the scratch anyway
        uint32_t bytes = count * stride;
        if (bytes > scratch_size) {
            if (scratch) mem::FreeSystem(scratch);
            scratch_size = (bytes + 0xFFFF) & ~0xFFFFu;
            scratch = mem::AllocSystem(scratch_size, 16);
        }
        if (scratch) {
            auto* out = static_cast<uint8_t*>(HostPtr(scratch));
            const auto* from = static_cast<const uint8_t*>(HostPtr(vertices));
            for (uint32_t i = 0; i < count; i++)
                std::memcpy(out + i * stride, from + uint32_t(G<uint16_t>(indices + i * 2)) * stride, stride);
            CallGuest(ctx, kDrawVerticesUP, {type, count, scratch, stride});
        }
    }
    Return(ctx, 0, 5);
}
