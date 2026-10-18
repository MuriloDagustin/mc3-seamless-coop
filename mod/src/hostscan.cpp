// "hostfind <hex> [max]": where the runner's own image (exe sections) holds a dword, with the 16 bytes after it.
// For finding the recompiler's guest-address -> native-function tables.
#include <cstdio>
#include <string>
#include <vector>

#include "mod.h"

namespace mod {

std::string HostFind(uint32_t value, int max) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    std::string out;
    char line[160];
    int hits = 0;
    for (int s = 0; s < nt->FileHeader.NumberOfSections && hits < max; s++) {
        uint8_t* p = base + sec[s].VirtualAddress;
        size_t n = sec[s].Misc.VirtualSize;
        for (size_t i = 0; i + 20 <= n && hits < max; i += 4) {
            if (*reinterpret_cast<uint32_t*>(p + i) != value) continue;
            const uint32_t* w = reinterpret_cast<uint32_t*>(p + i);
            std::snprintf(line, sizeof line, "%.8s+%zX (%p): %08X %08X %08X %08X %08X | prev %08X %08X\n",
                          reinterpret_cast<const char*>(sec[s].Name), size_t(sec[s].VirtualAddress + i), p + i, w[0], w[1],
                          w[2], w[3], w[4], i >= 8 ? w[-2] : 0, i >= 8 ? w[-1] : 0);
            out += line;
            hits++;
        }
    }
    char head[96];
    std::snprintf(head, sizeof head, "image %p, %d hits\n", base, hits);
    return head + out;
}

}  // namespace mod
