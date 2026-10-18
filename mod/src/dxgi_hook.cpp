// The runner presents through DXGI (its Vulkan frames handed to a D3D12 swapchain): the factory functions it imports
// are patched in its import table, and the swapchains they make get their Present wrapped (captures, the panel).
#include "mod.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "overlay.h"

namespace mod::dxgi {

namespace {

using CreateFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
CreateFactory1Fn g_factory1 = nullptr;
CreateFactory2Fn g_factory2 = nullptr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ForHwndFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                               const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using LegacyFn = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
PresentFn g_present = nullptr;
Present1Fn g_present1 = nullptr;
ForHwndFn g_for_hwnd = nullptr;
LegacyFn g_legacy = nullptr;
std::atomic<int> g_presents{0};

void Patch(void** slot, void* to, void** old) {
    DWORD protect;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &protect);
    if (old && !*old) *old = *slot;
    *slot = to;
    VirtualProtect(slot, sizeof(void*), protect, &protect);
}

HRESULT STDMETHODCALLTYPE Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    int n = ++g_presents;
    if (n <= 3) Log("dxgi: present %d", n);
    overlay::BeforePresent(sc);
    return g_present(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE Present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    int n = ++g_presents;
    if (n <= 3) Log("dxgi: present1 %d", n);
    overlay::BeforePresent(sc);
    return g_present1(sc, sync, flags, p);
}

void HookSwapchain(IDXGISwapChain* sc, IUnknown* queue) {
    void** vt = *reinterpret_cast<void***>(sc);
    Patch(&vt[8], reinterpret_cast<void*>(Present), reinterpret_cast<void**>(&g_present));
    IDXGISwapChain1* sc1 = nullptr;
    if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc1)))) {
        void** vt1 = *reinterpret_cast<void***>(sc1);
        Patch(&vt1[22], reinterpret_cast<void*>(Present1), reinterpret_cast<void**>(&g_present1));
        sc1->Release();
    }
    overlay::SwapchainMade(sc, queue);
}

HRESULT STDMETHODCALLTYPE ForHwnd(IDXGIFactory2* f, IUnknown* device, HWND hwnd, const DXGI_SWAP_CHAIN_DESC1* desc,
                                  const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fs, IDXGIOutput* output, IDXGISwapChain1** out) {
    HRESULT hr = g_for_hwnd(f, device, hwnd, desc, fs, output, out);
    Log("dxgi: swapchain for window %ux%u format %d buffers %u -> %08lX", desc->Width, desc->Height, int(desc->Format),
        desc->BufferCount, hr);
    if (SUCCEEDED(hr) && *out) HookSwapchain(*out, device);
    return hr;
}

HRESULT STDMETHODCALLTYPE Legacy(IDXGIFactory* f, IUnknown* device, DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) {
    HRESULT hr = g_legacy(f, device, desc, out);
    Log("dxgi: swapchain %ux%u -> %08lX", desc->BufferDesc.Width, desc->BufferDesc.Height, hr);
    if (SUCCEEDED(hr) && *out) HookSwapchain(*out, device);
    return hr;
}

void HookFactory(void* factory) {
    void** vt = *reinterpret_cast<void***>(factory);
    Patch(&vt[10], reinterpret_cast<void*>(Legacy), reinterpret_cast<void**>(&g_legacy));
    IDXGIFactory2* f2 = nullptr;
    if (SUCCEEDED(static_cast<IUnknown*>(factory)->QueryInterface(IID_PPV_ARGS(&f2)))) {
        void** vt2 = *reinterpret_cast<void***>(f2);
        Patch(&vt2[15], reinterpret_cast<void*>(ForHwnd), reinterpret_cast<void**>(&g_for_hwnd));
        f2->Release();
    }
}

HRESULT WINAPI Factory1(REFIID riid, void** out) {
    HRESULT hr = g_factory1(riid, out);
    if (SUCCEEDED(hr)) HookFactory(*out);
    return hr;
}

HRESULT WINAPI Factory2(UINT flags, REFIID riid, void** out) {
    HRESULT hr = g_factory2(flags, riid, out);
    if (SUCCEEDED(hr)) HookFactory(*out);
    return hr;
}

}  // namespace

// the runner's import of dll!name pointed at 'to'
bool PatchImportImpl(const char* dll, const char* name, void* to, void** old) {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (auto* d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<char*>(base + d->Name), dll)) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            auto* by = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(by->Name, name)) continue;
            Patch(reinterpret_cast<void**>(&slots->u1.Function), to, old);
            return true;
        }
    }
    return false;
}


void Install() {
    bool a = PatchImportImpl("dxgi.dll", "CreateDXGIFactory1", reinterpret_cast<void*>(Factory1), reinterpret_cast<void**>(&g_factory1));
    bool b = PatchImportImpl("dxgi.dll", "CreateDXGIFactory2", reinterpret_cast<void*>(Factory2), reinterpret_cast<void**>(&g_factory2));
    Log("dxgi: import hooks %s %s", a ? "factory1" : "-", b ? "factory2" : "-");
}

int Presents() { return g_presents; }

}  // namespace mod::dxgi

namespace mod {
bool PatchImport(const char* dll, const char* name, void* to, void** old) {
    return dxgi::PatchImportImpl(dll, name, to, old);
}
}  // namespace mod
