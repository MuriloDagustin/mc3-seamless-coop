// Test copies run on an invisible desktop, where no window is ever in front: the runner reads no controller and may
// hold its frames there. For those copies only (a control port set), its window is reported in front and visible.
#include "mod.h"

#include <atomic>
#include <thread>

namespace mod {

namespace {
using GetForegroundFn = HWND(WINAPI*)();
using IsIconicFn = BOOL(WINAPI*)(HWND);
using IsVisibleFn = BOOL(WINAPI*)(HWND);
GetForegroundFn g_foreground = nullptr;
IsIconicFn g_iconic = nullptr;
IsVisibleFn g_visible = nullptr;

bool Ours(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid == GetCurrentProcessId();
}

HWND WINAPI Foreground() {
    if (ManualMode()) return g_foreground ? g_foreground() : nullptr;
    HWND w = GameWindow();
    return w ? w : g_foreground();
}
BOOL WINAPI Iconic(HWND h) { return ManualMode() ? g_iconic(h) : (Ours(h) ? FALSE : g_iconic(h)); }
BOOL WINAPI Visible(HWND h) { return ManualMode() ? g_visible(h) : (Ours(h) ? TRUE : g_visible(h)); }
}  // namespace

void FakeFocus() {
    if (Env("MC3COOP_CONTROL").empty()) return;
    bool a = PatchImport("user32.dll", "GetForegroundWindow", reinterpret_cast<void*>(Foreground), reinterpret_cast<void**>(&g_foreground));
    bool b = PatchImport("user32.dll", "IsIconic", reinterpret_cast<void*>(Iconic), reinterpret_cast<void**>(&g_iconic));
    bool c = PatchImport("user32.dll", "IsWindowVisible", reinterpret_cast<void*>(Visible), reinterpret_cast<void**>(&g_visible));
    Log("focus: test copy, window reported in front (%d%d%d)", a, b, c);
}

}  // namespace mod

// "key": a key held on the test copy's own window for some ms, as a window message and in the key state queries
namespace mod {

namespace {
using KeyStateFn = SHORT(WINAPI*)(int);
KeyStateFn g_async = nullptr, g_state = nullptr;
std::atomic<int> g_key{0};
std::atomic<uint64_t> g_key_until{0};

uint64_t Now() { return GetTickCount64(); }
bool Held(int vk) { return vk && vk == g_key && Now() < g_key_until; }
SHORT WINAPI AsyncKey(int vk) { return ManualMode() || !Held(vk) ? g_async(vk) : SHORT(0x8001); }
SHORT WINAPI KeyState(int vk) { return ManualMode() || !Held(vk) ? g_state(vk) : SHORT(0x8000); }
}  // namespace

void FakeKeys() {
    if (Env("MC3COOP_CONTROL").empty()) return;
    PatchImport("user32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(AsyncKey), reinterpret_cast<void**>(&g_async));
    PatchImport("user32.dll", "GetKeyState", reinterpret_cast<void*>(KeyState), reinterpret_cast<void**>(&g_state));
}

void PressKey(int vk, int ms) {
    HWND w = GameWindow();
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    g_key = vk;
    g_key_until = Now() + ms;
    if (w) PostMessageW(w, WM_KEYDOWN, vk, 1 | (scan << 16));
    std::thread([w, vk, ms, scan] {
        Sleep(ms);
        if (w) PostMessageW(w, WM_KEYUP, vk, 1 | (scan << 16) | (1u << 30) | (1u << 31));
    }).detach();
}

}  // namespace mod
