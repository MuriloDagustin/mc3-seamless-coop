// Dear ImGui over the runner's D3D12 frame: drawn into the back buffer on the runner's own queue just before each
// present (overlay.cpp), only while there is something to show (the F10 panel, a session's status line). The game
// window is subclassed for F10 (the panel), F8 (take me to my partner) and, while the panel is open, ImGui's input.
#include "ui.h"

#include "mod.h"  // windows.h without min/max macros, before the D3D headers

#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <mutex>
#include <vector>

#include <imgui.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_win32.h>

#include "net/session.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace mod::ui {

namespace {

std::recursive_mutex g_lock;  // ImGui's state: the window thread's messages and the render thread's frame
bool g_open = false, g_ready = false, g_failed = false;
HWND g_window = nullptr;
WNDPROC g_game_proc = nullptr;
ID3D12Device* g_device = nullptr;
ID3D12DescriptorHeap* g_srv = nullptr;
ID3D12DescriptorHeap* g_rtv = nullptr;
UINT g_rtv_size = 0, g_buffers = 0;
DXGI_FORMAT g_format = DXGI_FORMAT_UNKNOWN;
struct Frame {
    ID3D12CommandAllocator* alloc = nullptr;
    UINT64 fence_value = 0;
};
std::vector<Frame> g_frames;
UINT g_frame = 0;
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Fence* g_fence = nullptr;
UINT64 g_fence_value = 0;
HANDLE g_event = nullptr;

LRESULT CALLBACK Proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && !(lp & (1 << 30))) {  // first press, not the repeats
        if (wp == VK_F10) {
            std::lock_guard lock(g_lock);
            g_open = !g_open;
            if (g_ready) ImGui::GetIO().MouseDrawCursor = g_open;
            return 0;
        }
        if (wp == VK_F8) {
            coop::RequestRescue();
            return 0;
        }
    }
    if (msg == WM_SYSKEYUP && wp == VK_F10) return 0;  // no menu bar activation
    {
        std::lock_guard lock(g_lock);
        if (g_open && g_ready) {
            if (ImGui_ImplWin32_WndProcHandler(w, msg, wp, lp)) return 1;
            ImGuiIO& io = ImGui::GetIO();
            bool mouse = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
            bool keys = msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP;
            if ((mouse && io.WantCaptureMouse) || (keys && io.WantCaptureKeyboard)) return 0;
        }
    }
    return CallWindowProcW(g_game_proc, w, msg, wp, lp);
}

bool Init(IDXGISwapChain3* sc, ID3D12Device* device) {
    DXGI_SWAP_CHAIN_DESC desc;
    if (FAILED(sc->GetDesc(&desc))) return false;
    g_window = desc.OutputWindow;
    g_buffers = desc.BufferCount;
    g_format = desc.BufferDesc.Format;
    g_device = device;
    D3D12_DESCRIPTOR_HEAP_DESC srv{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE};
    D3D12_DESCRIPTOR_HEAP_DESC rtv{D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_buffers, D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
    if (FAILED(device->CreateDescriptorHeap(&srv, IID_PPV_ARGS(&g_srv))) ||
        FAILED(device->CreateDescriptorHeap(&rtv, IID_PPV_ARGS(&g_rtv))))
        return false;
    g_rtv_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g_frames.resize(g_buffers);
    for (auto& f : g_frames)
        if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc)))) return false;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].alloc, nullptr, IID_PPV_ARGS(&g_list))) ||
        FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        return false;
    g_list->Close();
    g_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6, style.FrameRounding = 4;
    style.ScaleAllSizes(std::max(1.0f, float(desc.BufferDesc.Height) / 720.0f));
    io.FontGlobalScale = std::max(1.0f, float(desc.BufferDesc.Height) / 720.0f);
    ImGui_ImplWin32_Init(g_window);
    ImGui_ImplDX12_Init(device, int(g_buffers), g_format, g_srv, g_srv->GetCPUDescriptorHandleForHeapStart(),
                        g_srv->GetGPUDescriptorHandleForHeapStart());
    g_game_proc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Proc)));
    Log("ui: ready, %u buffers, format %d, window %p", g_buffers, int(g_format), g_window);
    return true;
}

}  // namespace

bool Open() { return g_open; }
void Toggle() {
    std::lock_guard lock(g_lock);
    g_open = !g_open;
    if (g_ready) ImGui::GetIO().MouseDrawCursor = g_open;
}

void Render(IDXGISwapChain3* sc, ID3D12CommandQueue* queue, ID3D12Device* device) {
    std::lock_guard lock(g_lock);
    if (g_failed) return;
    if (!g_ready) {
        if (!Init(sc, device)) {
            g_failed = true;
            Log("ui: could not set up the panel");
            return;
        }
        g_ready = true;
    }
    if (device != g_device) return;  // another device (a renderer restart): not drawn there
    bool status = coop::Networked();
    if (!g_open && !status) return;
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    DrawPanel(g_open);
    ImGui::Render();
    UINT index = sc->GetCurrentBackBufferIndex();
    if (index >= g_frames.size()) {
        ImGui::EndFrame();
        return;
    }
    ID3D12Resource* back = nullptr;
    if (FAILED(sc->GetBuffer(index, IID_PPV_ARGS(&back)))) return;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtv->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += SIZE_T(index) * g_rtv_size;
    device->CreateRenderTargetView(back, nullptr, rtv);
    Frame& f = g_frames[index];
    if (g_fence->GetCompletedValue() < f.fence_value) {  // that allocator's last list still on the GPU
        g_fence->SetEventOnCompletion(f.fence_value, g_event);
        WaitForSingleObject(g_event, 1000);
    }
    f.alloc->Reset();
    g_list->Reset(f.alloc, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = back;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_list->ResourceBarrier(1, &b);
    g_list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    g_list->SetDescriptorHeaps(1, &g_srv);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_list);
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    g_list->ResourceBarrier(1, &b);
    g_list->Close();
    ID3D12CommandList* lists[] = {g_list};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(g_fence, ++g_fence_value);
    f.fence_value = g_fence_value;
    back->Release();
}

}  // namespace mod::ui
