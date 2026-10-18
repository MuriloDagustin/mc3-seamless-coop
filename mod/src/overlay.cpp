// The DXGI side of the panel and the captures: the runner's D3D12 swapchain (XRECOMP_GPU_BACKEND=d3d12), its back
// buffer read back on the runner's own queue before each present that a capture asked for.
#include "overlay.h"

#include "mod.h"

#include <d3d12.h>
#include <dxgi1_6.h>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace mod::overlay {

namespace {

std::mutex g_lock;
IDXGISwapChain3* g_swapchain = nullptr;
ID3D12CommandQueue* g_queue = nullptr;
ID3D12Device* g_device = nullptr;
ID3D12CommandAllocator* g_alloc = nullptr;
ID3D12GraphicsCommandList* g_list = nullptr;
ID3D12Fence* g_fence = nullptr;
UINT64 g_fence_value = 0;
HANDLE g_fence_event = nullptr;

std::mutex g_shot_lock;
std::condition_variable g_shot_done;
std::wstring g_shot_path;
std::string g_shot_result;
bool g_shot_pending = false;

bool EnsureCommands() {
    if (g_list) return true;
    if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_alloc))) ||
        FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, nullptr, IID_PPV_ARGS(&g_list))) ||
        FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        return false;
    g_list->Close();
    g_fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

void Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    g_list->ResourceBarrier(1, &b);
}

void WaitGpu() {
    g_queue->Signal(g_fence, ++g_fence_value);
    if (g_fence->GetCompletedValue() < g_fence_value) {
        g_fence->SetEventOnCompletion(g_fence_value, g_fence_event);
        WaitForSingleObject(g_fence_event, 2000);
    }
}

std::string Capture(const std::wstring& path) {
    if (!g_swapchain || !g_queue || !g_device) return "error no D3D12 swapchain";
    if (!EnsureCommands()) return "error command list";
    ID3D12Resource* back = nullptr;
    if (FAILED(g_swapchain->GetBuffer(g_swapchain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&back)))) return "error buffer";
    D3D12_RESOURCE_DESC desc = back->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    UINT rows;
    UINT64 row_bytes, total;
    g_device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &row_bytes, &total);
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* readback = nullptr;
    if (FAILED(g_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&readback)))) {
        back->Release();
        return "error readback buffer";
    }
    g_alloc->Reset();
    g_list->Reset(g_alloc, nullptr);
    Transition(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION dst{readback, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    dst.PlacedFootprint = fp;
    D3D12_TEXTURE_COPY_LOCATION src{back, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    src.SubresourceIndex = 0;
    g_list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(back, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    g_list->Close();
    ID3D12CommandList* lists[] = {g_list};
    g_queue->ExecuteCommandLists(1, lists);
    WaitGpu();
    std::string result = "error map";
    uint8_t* p = nullptr;
    D3D12_RANGE range{0, SIZE_T(total)};
    if (SUCCEEDED(readback->Map(0, &range, reinterpret_cast<void**>(&p)))) {
        uint32_t w = uint32_t(desc.Width), h = desc.Height;
        bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        std::vector<uint8_t> px(size_t(w) * h * 4);
        for (uint32_t y = 0; y < h; y++) std::memcpy(&px[size_t(y) * w * 4], p + fp.Offset + size_t(y) * fp.Footprint.RowPitch, w * 4);
        readback->Unmap(0, nullptr);
        if (rgba)
            for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f) {
            BITMAPINFOHEADER ih{sizeof ih, LONG(w), -LONG(h), 1, 32, BI_RGB};
            BITMAPFILEHEADER fh{0x4D42, uint32_t(sizeof fh + sizeof ih + px.size()), 0, 0, uint32_t(sizeof fh + sizeof ih)};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&ih, sizeof ih, 1, f);
            fwrite(px.data(), px.size(), 1, f);
            fclose(f);
            result = "ok";
        } else {
            result = "error cannot write";
        }
    }
    readback->Release();
    back->Release();
    return result;
}

}  // namespace

void SwapchainMade(IDXGISwapChain* swapchain, IUnknown* queue) {
    std::lock_guard lock(g_lock);
    IDXGISwapChain3* sc3 = nullptr;
    ID3D12CommandQueue* q = nullptr;
    if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&sc3))) || !queue || FAILED(queue->QueryInterface(IID_PPV_ARGS(&q)))) {
        Log("overlay: the swapchain is not a D3D12 one");
        if (sc3) sc3->Release();
        return;
    }
    ID3D12Device* device = nullptr;
    q->GetDevice(IID_PPV_ARGS(&device));
    if (g_device != device) {  // a new device: the old command objects belong to the old one
        if (g_list) g_list->Release(), g_list = nullptr;
        if (g_alloc) g_alloc->Release(), g_alloc = nullptr;
        if (g_fence) g_fence->Release(), g_fence = nullptr;
    }
    if (g_swapchain) g_swapchain->Release();
    if (g_queue) g_queue->Release();
    if (g_device) g_device->Release();
    g_swapchain = sc3, g_queue = q, g_device = device;
}

void BeforePresent(IDXGISwapChain* swapchain) {
    std::lock_guard lock(g_lock);
    if (!g_swapchain) return;
    bool want;
    std::wstring path;
    {
        std::lock_guard shot(g_shot_lock);
        want = g_shot_pending;
        path = g_shot_path;
    }
    if (!want) return;
    std::string result = Capture(path);
    {
        std::lock_guard shot(g_shot_lock);
        g_shot_pending = false;
        g_shot_result = result;
    }
    g_shot_done.notify_all();
}

std::string Shot(const std::wstring& path) {
    std::unique_lock lock(g_shot_lock);
    g_shot_path = path;
    g_shot_result.clear();
    g_shot_pending = true;
    if (!g_shot_done.wait_for(lock, std::chrono::seconds(5), [] { return !g_shot_pending; })) {
        g_shot_pending = false;
        return "error no present in 5 s";
    }
    return g_shot_result;
}

bool Active() { return g_swapchain != nullptr; }

// the Vulkan layer's side: nothing drawn there yet
void SwapchainCreated(VkDevice, VkPhysicalDevice, const DeviceTable&, VkSwapchainKHR, VkFormat, VkExtent2D,
                      const std::vector<VkImage>&) {}
void SwapchainDestroyed(VkDevice, const DeviceTable&, VkSwapchainKHR) {}
void DeviceLost(VkDevice) {}
bool Draw(VkQueue, uint32_t, VkSwapchainKHR, uint32_t, uint32_t, const VkSemaphore*, VkSemaphore*) { return false; }

}  // namespace mod::overlay
