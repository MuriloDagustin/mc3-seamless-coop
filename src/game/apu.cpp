// The Xbox audio chip (APU) registers MC3's DirectSound reads directly.
// The SDK mixes DirectSound in software and maps no APU, but the game's DSOUND code still reads the
// global sample counter XGSCNT (0xFE80200C, 48 kHz) to time streams (sub_004085C0). A committed
// page with a counter that follows the host clock answers those reads.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <thread>

#include "game/mc3.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"
#include "core/log.h"

namespace mc3::apu {

constexpr uint32_t kBase = 0xFE800000, kSize = 0x80000;  // APU MMIO aperture
constexpr uint32_t kSampleCount = 0xFE80200C;            // NV_PAPU_XGSCNT

void Init() {
    if (!mem::Commit(kBase, kSize)) {
        LOGE("apu: could not map the APU registers");
        return;
    }
    std::thread([] {
        LARGE_INTEGER freq, start, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        auto* counter = reinterpret_cast<std::atomic<uint32_t>*>(kernel::HostPtr(kSampleCount));
        for (;;) {
            QueryPerformanceCounter(&now);
            uint64_t ticks = uint64_t(now.QuadPart - start.QuadPart);
            counter->store(uint32_t(ticks / freq.QuadPart * 48000 + ticks % freq.QuadPart * 48000 / freq.QuadPart),
                           std::memory_order_relaxed);
            Sleep(1);
        }
    }).detach();
    LOGR("apu: registers mapped, XGSCNT follows the host clock");
}

}  // namespace mc3::apu
