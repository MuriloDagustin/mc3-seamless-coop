#pragma once
#include <cstdint>
#include <cstring>

namespace mod::game {
// Verified runner build and only native call site of its XInputGetState import (RE-CORRIDA, native-abi).
constexpr uintptr_t kRunnerBase = 0x140000000ull;
constexpr uintptr_t kGuestInputReturn = 0x1404E35C4ull;
constexpr uint32_t kRunnerStamp = 0x6AD2B76C;
constexpr uint32_t kInputCallOffset = 0x4E35BE;
inline bool GuestInputBuild(uintptr_t base, uint32_t stamp, uint32_t imageSize, const uint8_t* call) {
    constexpr uint8_t expected[]{0xFF, 0x15, 0x0C, 0x21, 0x09, 0x02};
    return base == kRunnerBase && stamp == kRunnerStamp && imageSize >= kInputCallOffset + sizeof expected &&
           call && std::memcmp(call, expected, sizeof expected) == 0;
}
inline bool GuestInputReturn(uintptr_t address) { return address == kGuestInputReturn; }
}  // namespace mod::game
