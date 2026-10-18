// MC3 co-op mod (xinput1_4.dll proxy in the xrevive runner): shared helpers
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace mod {

extern uint8_t* g_base;  // host address of Xbox VA 0
void Log(const char* fmt, ...);
std::wstring ExeDir();
std::string Env(const char* name);
bool Mapped(uint32_t va, uint32_t bytes);  // every page committed and readable
HWND GameWindow();
std::string HostFind(uint32_t value, int max);  // the runner image's copies of a dword (hostscan.cpp)
bool PatchImport(const char* dll, const char* name, void* to, void** old);  // in the runner's import table
void FakeFocus();
void FakeKeys();                 // test copies: GetAsyncKeyState/GetKeyState answer the channel's keys
void PressKey(int vk, int ms);   // the test copy's window gets the key down, then up  // test copies: the runner sees its window in front and visible (focus.cpp)
std::string Shot(const std::wstring& path);  // the game window as a BMP: "ok" or "error ..."

template <typename T>
T& G(uint32_t va) { return *reinterpret_cast<T*>(g_base + va); }
inline uint32_t R32(uint32_t va) { uint32_t v; std::memcpy(&v, g_base + va, 4); return v; }
inline void W32(uint32_t va, uint32_t v) { std::memcpy(g_base + va, &v, 4); }
inline float RF(uint32_t va) { float v; std::memcpy(&v, g_base + va, 4); return v; }
inline void* HostPtr(uint32_t va) { return g_base + va; }

namespace control {
void Init();                                   // MC3COOP_CONTROL=<port>: the test channel on 127.0.0.1
bool Enabled();
bool Override(DWORD user, XINPUT_STATE* state);  // the pad the channel gives, controller 0; others unplugged
}  // namespace control

namespace game {
void Start();                                   // the guest image is found: the game side's thread
void Pad(DWORD user, const XINPUT_STATE* state);  // a controller read by the game (null: not connected)
void FiberTick();                               // on the game thread, from the xinput hook
std::string State();                            // key=value for the test channel
}  // namespace game

}  // namespace mod
