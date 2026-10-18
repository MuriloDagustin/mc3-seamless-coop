// The game side of the mod: what MC3 holds where, read and written through the guest image.
#include <cstdio>
#include <string>

#include <atomic>

#include "mod.h"
#include "overlay.h"

extern std::atomic<uint32_t> g_xinput_calls;

namespace mod::game {

void Start() {}

void Pad(DWORD, const XINPUT_STATE*) {}

std::string State() {
    char s[256];
    std::snprintf(s, sizeof s, "base=%p xinput=%u presents=%d", g_base, g_xinput_calls.load(), dxgi::Presents());
    return s;
}

}  // namespace mod::game
