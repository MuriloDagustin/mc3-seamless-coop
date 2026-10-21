// MC3 co-op mod for the xrevive runner: loaded as xinput1_4.dll beside a copy of the runner (the runner imports
// XInputGetState and XInputSetState by ordinal 2 and 3), forwarding both to the system's XInput.
// The Xbox address space is flat in the runner: host = guest base + Xbox VA (the XBE header at VA 0x10000).
#include "mod.h"
#include "overlay.h"
#include "guest_poll.h"
#include "race.h"
#include <intrin.h>

#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace mod {

uint8_t* g_base = nullptr;
static std::mutex g_log_lock;
static FILE* g_log = nullptr;

std::wstring ExeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    return s.substr(0, s.find_last_of(L"\\/"));
}

std::string Env(const char* name) {
    char v[512];
    DWORD n = GetEnvironmentVariableA(name, v, sizeof v);
    return n && n < sizeof v ? std::string(v, n) : std::string{};
}

void Log(const char* fmt, ...) {
    std::lock_guard lock(g_log_lock);
    if (!g_log) {
        std::wstring dir = ExeDir() + L"\\logs";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::string inst = Env("MC3COOP_INSTANCE");
        std::wstring name = dir + L"\\mc3coop" + (inst.empty() ? L"" : L"-" + std::wstring(inst.begin(), inst.end()));
        MoveFileExW((name + L".log").c_str(), (name + L".prev.log").c_str(), MOVEFILE_REPLACE_EXISTING);  // last run kept
        g_log = _wfopen((name + L".log").c_str(), L"w");
        if (!g_log) return;
    }
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a;
    va_start(a, fmt);
    std::vfprintf(g_log, fmt, a);
    va_end(a);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

// VirtualQuery measures the whole run of alike pages around the address, a slow walk in the 4 GB guest image: the
// hundreds of checks of a game tick with a partner took 25 ms (7 fps, both PCs). The runs it reports are kept a
// moment per thread: the same tick now costs under 1 ms.
bool Mapped(uint32_t va, uint32_t bytes) {
    if (!g_base || !bytes) return false;
    struct Run { uint64_t begin, end; };
    thread_local Run runs[16];
    thread_local int count = 0, next = 0;
    thread_local uint64_t since = 0;
    uint64_t now = GetTickCount64();
    if (now - since > 20) count = 0, next = 0, since = now;
    uint64_t end = uint64_t(va) + bytes;
    for (uint64_t at = va & ~0xFFFull; at < end;) {
        int i = 0;
        while (i < count && !(runs[i].begin <= at && at < runs[i].end)) i++;
        if (i < count) {
            at = runs[i].end;
            continue;
        }
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(g_base + at, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT ||
            (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        Run r{uint64_t(static_cast<uint8_t*>(mbi.BaseAddress) - g_base), 0};
        r.end = r.begin + mbi.RegionSize;
        runs[count < 16 ? count++ : next++ % 16] = r;
        at = r.end;
    }
    return true;
}

// the 4 GB guest image: a page whose +0x10000 holds the XBE header (base address 0x10000). The runner also keeps a
// view of the physical RAM (low, at 0xA000000 in its log) with the same header; the guest VA image is the high one.
static uint8_t* FindBase() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uint8_t* best = nullptr;
    for (auto* p = static_cast<uint8_t*>(si.lpMinimumApplicationAddress); p < si.lpMaximumApplicationAddress;) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(p, &mbi, sizeof mbi)) break;
        auto* region = static_cast<uint8_t*>(mbi.BaseAddress);
        if (mbi.State == MEM_COMMIT && !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && mbi.RegionSize >= 0x1000) {
            for (uint8_t* page = region; page < region + mbi.RegionSize; page += 0x1000) {
                if (reinterpret_cast<uintptr_t>(page) & 0xFFFF) continue;  // VA 0x10000 is 64 KB aligned
                if (std::memcmp(page, "XBEH", 4) == 0 && *reinterpret_cast<uint32_t*>(page + 0x104) == 0x10000) {
                    Log("mc3coop: XBE header at %p", page);
                    if (page - 0x10000 > best) best = page - 0x10000;
                }
            }
        }
        p = region + mbi.RegionSize;
    }
    return best;
}

// the test copies are muted: this process's audio sessions at volume 0, again as the game opens new ones
static void MuteLoop() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    for (int tries = 0;; tries++) {
        IMMDeviceEnumerator* en = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) {
            IMMDevice* dev = nullptr;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev))) {
                IAudioSessionManager2* mgr = nullptr;
                if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr)))) {
                    IAudioSessionEnumerator* list = nullptr;
                    if (SUCCEEDED(mgr->GetSessionEnumerator(&list))) {
                        int n = 0;
                        list->GetCount(&n);
                        for (int i = 0; i < n; i++) {
                            IAudioSessionControl* c = nullptr;
                            IAudioSessionControl2* c2 = nullptr;
                            if (SUCCEEDED(list->GetSession(i, &c)) &&
                                SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c2)))) {
                                DWORD pid = 0;
                                c2->GetProcessId(&pid);
                                ISimpleAudioVolume* v = nullptr;
                                if (pid == GetCurrentProcessId() && SUCCEEDED(c2->QueryInterface(IID_PPV_ARGS(&v)))) {
                                    v->SetMute(TRUE, nullptr);
                                    v->Release();
                                }
                            }
                            if (c2) c2->Release();
                            if (c) c->Release();
                        }
                        list->Release();
                    }
                    mgr->Release();
                }
                dev->Release();
            }
            en->Release();
        }
        Sleep(tries < 60 ? 500 : 3000);
    }
}

static void Start() {
    Log("mc3coop: loaded in pid %lu", GetCurrentProcessId());
    if (Env("MC3COOP_MUTE") == "1") std::thread(MuteLoop).detach();
    control::Init();
    for (int i = 0; i < 600 && !g_base; i++) {
        g_base = FindBase();
        if (!g_base) Sleep(100);
    }
    if (!g_base) {
        Log("mc3coop: guest image not found");
        return;
    }
    Log("mc3coop: guest base %p, title %08X", g_base, R32(R32(0x10118) + 8));
    game::Start();
}

}  // namespace mod

// the system XInput, for the game's own controllers; the whole API is forwarded (SDL, inside the runner, asks for
// the capabilities and the Ex ordinals before it uses a controller), and a test copy gets the channel's virtual pad
// as controller 0, the others unplugged
static HMODULE g_xinput = nullptr;
static FARPROC Real(const char* name_or_ordinal) {
    if (!g_xinput) {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\xinput1_4.dll");
        g_xinput = LoadLibraryW(path);
    }
    return g_xinput ? GetProcAddress(g_xinput, name_or_ordinal) : nullptr;
}
#define REAL(type, name) reinterpret_cast<type>(Real(name))

std::atomic<uint32_t> g_xinput_calls{0}, g_xinput_guest{0};
std::atomic<uintptr_t> g_xinput_ret{0};

static DWORD VirtualState(DWORD user, XINPUT_STATE* state) {
    if (user != 0) return ERROR_DEVICE_NOT_CONNECTED;
    mod::control::Override(user, state);
    return ERROR_SUCCESS;
}

namespace {
bool GuestInputContext(uintptr_t returnAddress) {
    static const uintptr_t guest = [] {
        uintptr_t r = mod::game::FindGuestInputReturn(reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr)));
        if (r) mod::Log("game: guest pad poll returns to %llX", static_cast<unsigned long long>(r));
        else mod::Log("game: no single guest XInputGetState call in this runner; race/traffic/AI writes off");
        return r;
    }();
    return guest && returnAddress == guest;
}
}  // namespace

extern "C" DWORD WINAPI ProxyXInputGetState(DWORD user, XINPUT_STATE* state) {
    g_xinput_calls++;
    const bool guest = GuestInputContext(reinterpret_cast<uintptr_t>(_ReturnAddress()));
    g_xinput_ret = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (guest) g_xinput_guest++, mod::game::FiberTick();
    DWORD r;
    if (mod::control::Enabled() && !mod::ManualMode()) r = VirtualState(user, state);
    else {
        auto f = REAL(DWORD(WINAPI*)(DWORD, XINPUT_STATE*), "XInputGetState");
        r = f ? f(user, state) : ERROR_DEVICE_NOT_CONNECTED;
    }
    // Filter real buttons first. The native marker still needs the mod's headlight button after a result
    // has returned to cruise, including while a stale held confirmation is being suppressed.
    if (guest && user == 0 && r == ERROR_SUCCESS && state) {
        uint16_t buttons = state->Gamepad.wButtons;
        if (mod::race::ControlInput(buttons)) state->Gamepad = {};
        state->Gamepad.wButtons = buttons;
        if (mod::race::ChallengeInput()) state->Gamepad.wButtons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    }
    mod::game::Pad(user, r == ERROR_SUCCESS ? state : nullptr);
    return r;
}

extern "C" DWORD WINAPI ProxyXInputGetStateEx(DWORD user, XINPUT_STATE* state) {  // ordinal 100: with the guide button
    g_xinput_calls++;
    g_xinput_ret = reinterpret_cast<uintptr_t>(_ReturnAddress()) | 1;  // odd: from the Ex entry
    if (mod::control::Enabled() && !mod::ManualMode()) return VirtualState(user, state);
    auto f = REAL(DWORD(WINAPI*)(DWORD, XINPUT_STATE*), MAKEINTRESOURCEA(100));
    DWORD r = f ? f(user, state) : ERROR_DEVICE_NOT_CONNECTED;
    mod::game::Pad(user, r == ERROR_SUCCESS ? state : nullptr);
    return r;
}

extern "C" DWORD WINAPI ProxyXInputSetState(DWORD user, XINPUT_VIBRATION* vibration) {
    if (mod::control::Enabled() && !mod::ManualMode()) return user == 0 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;  // no rumble for tests
    auto f = REAL(DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*), "XInputSetState");
    return f ? f(user, vibration) : ERROR_DEVICE_NOT_CONNECTED;
}

static DWORD VirtualCaps(DWORD user, XINPUT_CAPABILITIES* caps) {
    if (user != 0) return ERROR_DEVICE_NOT_CONNECTED;
    std::memset(caps, 0, sizeof *caps);
    caps->Type = XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 255;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = -64;
    return ERROR_SUCCESS;
}

extern "C" DWORD WINAPI ProxyXInputGetCapabilities(DWORD user, DWORD flags, XINPUT_CAPABILITIES* caps) {
    if (mod::control::Enabled() && !mod::ManualMode()) return VirtualCaps(user, caps);
    auto f = REAL(DWORD(WINAPI*)(DWORD, DWORD, XINPUT_CAPABILITIES*), "XInputGetCapabilities");
    return f ? f(user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

// ordinal 108, XInputGetCapabilitiesEx(1, user, flags, XINPUT_CAPABILITIES_EX*): the caps then vendor/product ids
extern "C" DWORD WINAPI ProxyXInputGetCapabilitiesEx(DWORD a, DWORD user, DWORD flags, void* caps) {
    if (mod::control::Enabled() && !mod::ManualMode()) {
        if (user != 0) return ERROR_DEVICE_NOT_CONNECTED;
        std::memset(caps, 0, sizeof(XINPUT_CAPABILITIES) + 8);
        VirtualCaps(0, static_cast<XINPUT_CAPABILITIES*>(caps));
        auto* ids = reinterpret_cast<WORD*>(static_cast<uint8_t*>(caps) + sizeof(XINPUT_CAPABILITIES));
        ids[0] = 0x045E, ids[1] = 0x028E, ids[2] = 0x0114;  // an Xbox 360 controller
        return ERROR_SUCCESS;
    }
    auto f = REAL(DWORD(WINAPI*)(DWORD, DWORD, DWORD, void*), MAKEINTRESOURCEA(108));
    return f ? f(a, user, flags, caps) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" void WINAPI ProxyXInputEnable(BOOL enable) {
    auto f = REAL(void(WINAPI*)(BOOL), "XInputEnable");
    if (f) f(enable);
}

extern "C" DWORD WINAPI ProxyXInputGetBatteryInformation(DWORD user, BYTE type, XINPUT_BATTERY_INFORMATION* info) {
    if (mod::control::Enabled() && !mod::ManualMode()) {
        if (user != 0) return ERROR_DEVICE_NOT_CONNECTED;
        info->BatteryType = BATTERY_TYPE_WIRED, info->BatteryLevel = BATTERY_LEVEL_FULL;
        return ERROR_SUCCESS;
    }
    auto f = REAL(DWORD(WINAPI*)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*), "XInputGetBatteryInformation");
    return f ? f(user, type, info) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI ProxyXInputGetKeystroke(DWORD user, DWORD reserved, PXINPUT_KEYSTROKE key) {
    if (mod::control::Enabled() && !mod::ManualMode()) return user == 0 ? ERROR_EMPTY : ERROR_DEVICE_NOT_CONNECTED;
    auto f = REAL(DWORD(WINAPI*)(DWORD, DWORD, PXINPUT_KEYSTROKE), "XInputGetKeystroke");
    return f ? f(user, reserved, key) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI ProxyXInputGetAudioDeviceIds(DWORD user, LPWSTR render, UINT* rc, LPWSTR capture, UINT* cc) {
    auto f = REAL(DWORD(WINAPI*)(DWORD, LPWSTR, UINT*, LPWSTR, UINT*), "XInputGetAudioDeviceIds");
    return f ? f(user, render, rc, capture, cc) : ERROR_DEVICE_NOT_CONNECTED;
}

extern "C" DWORD WINAPI ProxyXInputWaitForGuideButton(DWORD user, DWORD flags, void* data) {  // ordinal 101
    auto f = REAL(DWORD(WINAPI*)(DWORD, DWORD, void*), MAKEINTRESOURCEA(101));
    return f ? f(user, flags, data) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyXInputCancelGuideButtonWait(DWORD user) {  // ordinal 102
    auto f = REAL(DWORD(WINAPI*)(DWORD), MAKEINTRESOURCEA(102));
    return f ? f(user) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyXInputPowerOffController(DWORD user) {  // ordinal 103
    auto f = REAL(DWORD(WINAPI*)(DWORD), MAKEINTRESOURCEA(103));
    return f ? f(user) : ERROR_DEVICE_NOT_CONNECTED;
}
extern "C" DWORD WINAPI ProxyXInputGetBaseBusInformation(DWORD user, void* info) {  // ordinal 104
    auto f = REAL(DWORD(WINAPI*)(DWORD, void*), MAKEINTRESOURCEA(104));
    return f ? f(user, info) : ERROR_DEVICE_NOT_CONNECTED;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        mod::dxgi::Install();  // before the runner's entry point makes its factory
        mod::FakeFocus();
        mod::FakeKeys();
        std::thread(mod::Start).detach();
    }
    return TRUE;
}
