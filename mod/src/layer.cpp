// The mod as a Vulkan layer (VK_LAYER_MC3COOP_overlay, enabled through VK_INSTANCE_LAYERS by our launcher): the
// runner's renderer (Plume) presents through it, so the co-op panel can be drawn into the game's frame and test
// copies on a hidden desktop can be captured (PrintWindow sees nothing there). The same DLL is the xinput proxy.
#include "mod.h"

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vulkan.h"
#include "vulkan/vk_layer.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "overlay.h"

namespace mod::layer {

namespace {

template <typename T>
void* Key(T handle) { return *reinterpret_cast<void**>(handle); }

struct Instance {
    VkInstance handle = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkDestroyInstance DestroyInstance = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties GetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties GetPhysicalDeviceQueueFamilyProperties = nullptr;
};

struct Device {
    VkDevice handle = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    Instance* instance = nullptr;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    DeviceTable t{};
    VkCommandPool pool = VK_NULL_HANDLE;
    uint32_t pool_family = ~0u;
    VkFence fence = VK_NULL_HANDLE;
};

struct Swapchain {
    VkDevice device = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::vector<VkImage> images;
};

std::mutex g_lock;
std::unordered_map<void*, Instance*> g_instances;
std::unordered_map<void*, Device*> g_devices;
std::unordered_map<VkSwapchainKHR, Swapchain> g_swapchains;
std::unordered_map<VkQueue, uint32_t> g_queue_family;
std::atomic<bool> g_active{false};

// a capture asked by the test channel, done at the next present
std::mutex g_shot_lock;
std::condition_variable g_shot_done;
std::wstring g_shot_path;
std::string g_shot_result;
bool g_shot_pending = false;

Instance* FindInstance(void* key) {
    std::lock_guard lock(g_lock);
    auto it = g_instances.find(key);
    return it == g_instances.end() ? nullptr : it->second;
}
Device* FindDevice(void* key) {
    std::lock_guard lock(g_lock);
    auto it = g_devices.find(key);
    return it == g_devices.end() ? nullptr : it->second;
}

uint32_t MemoryType(Device& d, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties p;
    d.instance->GetPhysicalDeviceMemoryProperties(d.physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (p.memoryTypes[i].propertyFlags & want) == want) return i;
    return ~0u;
}

bool EnsurePool(Device& d, uint32_t family) {
    if (d.pool && d.pool_family == family) return true;
    if (d.pool) d.t.DestroyCommandPool(d.handle, d.pool, nullptr);
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = family;
    if (d.t.CreateCommandPool(d.handle, &ci, nullptr, &d.pool) != VK_SUCCESS) return false;
    d.pool_family = family;
    if (!d.fence) {
        VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        d.t.CreateFence(d.handle, &fi, nullptr, &d.fence);
    }
    return true;
}

void Barrier(Device& d, VkCommandBuffer cb, VkImage image, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
             VkAccessFlags dst) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = src, b.dstAccessMask = dst;
    b.oldLayout = from, b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    d.t.CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &b);
}

// the presented image to a BMP; the present's semaphores are waited on here, the present then waits on nothing
std::string Capture(Device& d, VkQueue queue, const Swapchain& sc, VkImage image, const VkPresentInfoKHR* info,
                    const std::wstring& path) {
    uint32_t family = g_queue_family.count(queue) ? g_queue_family[queue] : 0;
    if (!EnsurePool(d, family)) return "error command pool";
    uint32_t w = sc.extent.width, h = sc.extent.height;
    VkDeviceSize bytes = VkDeviceSize(w) * h * 4;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;
    if (d.t.CreateBuffer(d.handle, &bi, nullptr, &buffer) != VK_SUCCESS) return "error buffer";
    VkMemoryRequirements req;
    d.t.GetBufferMemoryRequirements(d.handle, buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = MemoryType(d, req.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkDeviceMemory memory;
    if (ai.memoryTypeIndex == ~0u || d.t.AllocateMemory(d.handle, &ai, nullptr, &memory) != VK_SUCCESS) {
        d.t.DestroyBuffer(d.handle, buffer, nullptr);
        return "error memory";
    }
    d.t.BindBufferMemory(d.handle, buffer, memory, 0);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = d.pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    d.t.AllocateCommandBuffers(d.handle, &cai, &cb);
    *reinterpret_cast<void**>(cb) = *reinterpret_cast<void**>(d.handle);  // a layer's own command buffer: the loader's dispatch
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    d.t.BeginCommandBuffer(cb, &begin);
    Barrier(d, cb, image, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_READ_BIT,
            VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {w, h, 1};
    d.t.CmdCopyImageToBuffer(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    Barrier(d, cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT);
    d.t.EndCommandBuffer(cb);
    std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = info->waitSemaphoreCount;
    si.pWaitSemaphores = info->pWaitSemaphores;
    si.pWaitDstStageMask = stages.data();
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    d.t.ResetFences(d.handle, 1, &d.fence);
    std::string result = "error submit";
    if (d.t.QueueSubmit(queue, 1, &si, d.fence) == VK_SUCCESS &&
        d.t.WaitForFences(d.handle, 1, &d.fence, VK_TRUE, 2000000000ull) == VK_SUCCESS) {
        void* p = nullptr;
        d.t.MapMemory(d.handle, memory, 0, bytes, 0, &p);
        bool rgba = sc.format == VK_FORMAT_R8G8B8A8_UNORM || sc.format == VK_FORMAT_R8G8B8A8_SRGB;
        std::vector<uint8_t> px(static_cast<uint8_t*>(p), static_cast<uint8_t*>(p) + bytes);
        d.t.UnmapMemory(d.handle, memory);
        if (rgba)
            for (size_t i = 0; i < px.size(); i += 4) std::swap(px[i], px[i + 2]);
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f) {
            BITMAPINFOHEADER ih{sizeof ih, LONG(w), -LONG(h), 1, 32, BI_RGB};
            BITMAPFILEHEADER fh{0x4D42, uint32_t(sizeof fh + sizeof ih + bytes), 0, 0, uint32_t(sizeof fh + sizeof ih)};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&ih, sizeof ih, 1, f);
            fwrite(px.data(), px.size(), 1, f);
            fclose(f);
            result = "ok";
        } else {
            result = "error cannot write";
        }
    }
    d.t.FreeCommandBuffers(d.handle, d.pool, 1, &cb);
    d.t.DestroyBuffer(d.handle, buffer, nullptr);
    d.t.FreeMemory(d.handle, memory, nullptr);
    return result;
}

// --- the layer's entry points

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc,
                                              VkInstance* out) {
    auto* link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(ci->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(VK_NULL_HANDLE, "vkCreateInstance"));
    VkResult r = create(ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    auto* inst = new Instance;
    inst->handle = *out;
    inst->gipa = gipa;
    inst->DestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(gipa(*out, "vkDestroyInstance"));
    inst->GetPhysicalDeviceMemoryProperties =
        reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(*out, "vkGetPhysicalDeviceMemoryProperties"));
    inst->GetPhysicalDeviceQueueFamilyProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        gipa(*out, "vkGetPhysicalDeviceQueueFamilyProperties"));
    std::lock_guard lock(g_lock);
    g_instances[Key(*out)] = inst;
    g_active = true;
    Log("layer: instance created");
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
    Instance* inst = FindInstance(Key(instance));
    if (!inst) return;
    inst->DestroyInstance(instance, alloc);
    std::lock_guard lock(g_lock);
    g_instances.erase(Key(instance));
    delete inst;
}

#define LOAD(name) d->t.name = reinterpret_cast<PFN_vk##name>(gdpa(*out, "vk" #name));

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice physical, const VkDeviceCreateInfo* ci,
                                            const VkAllocationCallbacks* alloc, VkDevice* out) {
    auto* link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(ci->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    PFN_vkGetInstanceProcAddr gipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    Instance* inst = FindInstance(Key(physical));
    auto create = reinterpret_cast<PFN_vkCreateDevice>(gipa(inst ? inst->handle : VK_NULL_HANDLE, "vkCreateDevice"));
    VkResult r = create(physical, ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    auto* d = new Device;
    d->handle = *out;
    d->physical = physical;
    d->instance = inst;
    d->gdpa = gdpa;
    MOD_DEVICE_FUNCTIONS(LOAD)
    std::lock_guard lock(g_lock);
    g_devices[Key(*out)] = d;
    Log("layer: device created");
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
    Device* d = FindDevice(Key(device));
    if (!d) return;
    overlay::DeviceLost(d->handle);
    if (d->fence) d->t.DestroyFence(device, d->fence, nullptr);
    if (d->pool) d->t.DestroyCommandPool(device, d->pool, nullptr);
    d->t.DestroyDevice(device, alloc);
    std::lock_guard lock(g_lock);
    g_devices.erase(Key(device));
    delete d;
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue(VkDevice device, uint32_t family, uint32_t index, VkQueue* queue) {
    Device* d = FindDevice(Key(device));
    d->t.GetDeviceQueue(device, family, index, queue);
    std::lock_guard lock(g_lock);
    g_queue_family[*queue] = family;
}

VKAPI_ATTR void VKAPI_CALL GetDeviceQueue2(VkDevice device, const VkDeviceQueueInfo2* info, VkQueue* queue) {
    Device* d = FindDevice(Key(device));
    d->t.GetDeviceQueue2(device, info, queue);
    std::lock_guard lock(g_lock);
    g_queue_family[*queue] = info->queueFamilyIndex;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* ci,
                                                  const VkAllocationCallbacks* alloc, VkSwapchainKHR* out) {
    Device* d = FindDevice(Key(device));
    VkSwapchainCreateInfoKHR mine = *ci;
    mine.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;  // captures, the panel
    VkResult r = d->t.CreateSwapchainKHR(device, &mine, alloc, out);
    if (r != VK_SUCCESS) r = d->t.CreateSwapchainKHR(device, ci, alloc, out);  // the plain one, without our uses
    if (r != VK_SUCCESS) return r;
    Swapchain sc;
    sc.device = device;
    sc.format = ci->imageFormat;
    sc.extent = ci->imageExtent;
    uint32_t n = 0;
    d->t.GetSwapchainImagesKHR(device, *out, &n, nullptr);
    sc.images.resize(n);
    d->t.GetSwapchainImagesKHR(device, *out, &n, sc.images.data());
    {
        std::lock_guard lock(g_lock);
        g_swapchains[*out] = sc;
    }
    overlay::SwapchainCreated(device, d->physical, d->t, *out, ci->imageFormat, ci->imageExtent, sc.images);
    Log("layer: swapchain %ux%u format %d, %u images", ci->imageExtent.width, ci->imageExtent.height, int(ci->imageFormat), n);
    return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks* alloc) {
    Device* d = FindDevice(Key(device));
    overlay::SwapchainDestroyed(device, d->t, swapchain);
    {
        std::lock_guard lock(g_lock);
        g_swapchains.erase(swapchain);
    }
    d->t.DestroySwapchainKHR(device, swapchain, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    Device* d = FindDevice(Key(queue));
    static std::atomic<int> logged{0};
    if (logged < 3) Log("layer: present %d (device %s)", ++logged, d ? "known" : "UNKNOWN");
    if (!d || !info->swapchainCount) return d ? d->t.QueuePresentKHR(queue, info) : VK_ERROR_DEVICE_LOST;
    Swapchain sc;
    {
        std::lock_guard lock(g_lock);
        auto it = g_swapchains.find(info->pSwapchains[0]);
        if (it != g_swapchains.end()) sc = it->second;
    }
    VkPresentInfoKHR mine = *info;
    uint32_t family = 0;
    {
        std::lock_guard lock(g_lock);
        if (g_queue_family.count(queue)) family = g_queue_family[queue];
    }
    VkImage image = sc.images.empty() ? VK_NULL_HANDLE : sc.images[info->pImageIndices[0]];
    // the panel over the game's frame; it waits on the present's semaphores and hands one back
    VkSemaphore drawn = VK_NULL_HANDLE;
    if (image && overlay::Draw(queue, family, info->pSwapchains[0], info->pImageIndices[0], info->waitSemaphoreCount,
                               info->pWaitSemaphores, &drawn)) {
        mine.waitSemaphoreCount = 1;
        mine.pWaitSemaphores = &drawn;
    }
    bool want;
    std::wstring path;
    {
        std::lock_guard lock(g_shot_lock);
        want = g_shot_pending;
        path = g_shot_path;
    }
    if (want && image) {
        std::string result = Capture(*d, queue, sc, image, &mine, path);
        mine.waitSemaphoreCount = 0;  // the capture waited on them and on its own fence
        {
            std::lock_guard lock(g_shot_lock);
            g_shot_pending = false;
            g_shot_result = result;
        }
        g_shot_done.notify_all();
    }
    return d->t.QueuePresentKHR(queue, &mine);
}

VKAPI_ATTR VkResult VKAPI_CALL AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                                   VkSemaphore semaphore, VkFence fence, uint32_t* index) {
    Device* d = FindDevice(Key(device));
    static auto next = reinterpret_cast<PFN_vkAcquireNextImageKHR>(d->gdpa(device, "vkAcquireNextImageKHR"));
    VkResult r = next(device, swapchain, timeout, semaphore, fence, index);
    static std::atomic<int> logged{0};
    if (logged < 5) Log("layer: acquire %d -> %d", ++logged, int(r));
    return r;
}

PFN_vkVoidFunction Intercept(const char* name) {
    static std::atomic<int> asked{0};
    if (++asked <= 400) Log("layer: asked for %s", name);
    if (!std::strcmp(name, "vkAcquireNextImageKHR")) return reinterpret_cast<PFN_vkVoidFunction>(AcquireNextImageKHR);
#define HOOK(fn) if (!std::strcmp(name, "vk" #fn)) return reinterpret_cast<PFN_vkVoidFunction>(fn);
    HOOK(CreateInstance) HOOK(DestroyInstance) HOOK(CreateDevice) HOOK(DestroyDevice) HOOK(GetDeviceQueue)
    HOOK(GetDeviceQueue2) HOOK(CreateSwapchainKHR) HOOK(DestroySwapchainKHR) HOOK(QueuePresentKHR)
#undef HOOK
    return nullptr;
}

}  // namespace

bool Active() { return g_active; }

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

const Instance* AnyInstance() {
    std::lock_guard lock(g_lock);
    return g_instances.empty() ? nullptr : g_instances.begin()->second;
}

}  // namespace mod::layer

using namespace mod::layer;

extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL MC3Coop_GetDeviceProcAddr(VkDevice device,
                                                                                                    const char* name) {
    if (auto f = Intercept(name)) return f;
    Device* d = FindDevice(Key(device));
    return d ? d->gdpa(device, name) : nullptr;
}

extern "C" __declspec(dllexport) VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL MC3Coop_GetInstanceProcAddr(VkInstance instance,
                                                                                                      const char* name) {
    if (auto f = Intercept(name)) return f;
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(MC3Coop_GetDeviceProcAddr);
    if (!std::strcmp(name, "vkGetInstanceProcAddr"))
        return reinterpret_cast<PFN_vkVoidFunction>(MC3Coop_GetInstanceProcAddr);
    Instance* inst = instance ? FindInstance(Key(instance)) : nullptr;
    return inst ? inst->gipa(instance, name) : nullptr;
}

extern "C" __declspec(dllexport) VKAPI_ATTR VkResult VKAPI_CALL
vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v) {
    if (v->loaderLayerInterfaceVersion > 2) v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = MC3Coop_GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = MC3Coop_GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}
