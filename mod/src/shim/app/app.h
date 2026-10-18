// The few SDK app calls the network files (from the Black co-op) use, answered by the mod
#pragma once
#include <filesystem>
#include <string>

namespace mod {
std::wstring ExeDir();
std::string Env(const char* name);
}
namespace app {
inline std::filesystem::path ExeDir() { return mod::ExeDir(); }
inline int Instance() { return std::atoi(mod::Env("MC3COOP_INSTANCE").c_str()); }
void* Window();  // the game window (shot.cpp)
}  // namespace app
