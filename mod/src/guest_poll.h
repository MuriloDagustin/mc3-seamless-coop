#pragma once
// The runner's guest pad poll: its only native call of the XInputGetState import (xinput1_4 ordinal 2,
// `call [rip+iat]`, RE-CORRIDA native ABI). That call runs on the game fiber, inside guest code, so the mod's game
// work (race, traffic, AI writes) is done there. Found in the loaded runner by its import table rather than by a
// fixed address, so a new xrevive build keeps working: exactly one such call site, or none is trusted.
#include <cstdint>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace mod::game {

// the return address of the runner's guest XInputGetState call, 0 when it is not found exactly once
inline uintptr_t FindGuestInputReturn(const uint8_t* base) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x100000) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 0;
    // the IAT slot of xinput1_4 ordinal 2 (or the name XInputGetState)
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return 0;
    uintptr_t slot = 0;
    for (auto* d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<const char*>(base + d->Name), "xinput1_4.dll")) continue;
        auto* names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        for (uint32_t i = 0; names[i].u1.AddressOfData; i++) {
            bool hit = IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)
                           ? IMAGE_ORDINAL64(names[i].u1.Ordinal) == 2
                           : !std::strcmp(reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData)->Name,
                                          "XInputGetState");
            if (hit) slot = reinterpret_cast<uintptr_t>(base) + d->FirstThunk + i * sizeof(IMAGE_THUNK_DATA64);
        }
    }
    if (!slot) return 0;
    // the code sections' `FF 15 rel32` calls through that slot
    uintptr_t found = 0;
    int count = 0;
    const auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int s = 0; s < nt->FileHeader.NumberOfSections; s++) {
        if (!(sec[s].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* p = base + sec[s].VirtualAddress;
        size_t n = sec[s].Misc.VirtualSize;
        for (size_t i = 0; i + 6 <= n; i++) {
            if (p[i] != 0xFF || p[i + 1] != 0x15) continue;
            int32_t rel;
            std::memcpy(&rel, p + i + 2, 4);
            if (reinterpret_cast<uintptr_t>(p + i + 6) + intptr_t(rel) == slot) found = reinterpret_cast<uintptr_t>(p + i + 6), count++;
        }
    }
    return count == 1 ? found : 0;
}

}  // namespace mod::game
