// The co-op panel drawn into the game's frame by the Vulkan layer (layer.cpp), with Dear ImGui
#pragma once
#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include "vulkan/vulkan.h"

#include <string>
#include <vector>

// the device functions the layer and the panel call down the chain
#define MOD_DEVICE_FUNCTIONS(X)                                                                                        \
    X(DestroyDevice) X(GetDeviceQueue) X(GetDeviceQueue2) X(CreateSwapchainKHR) X(DestroySwapchainKHR)                  \
    X(GetSwapchainImagesKHR) X(QueuePresentKHR) X(QueueSubmit) X(CreateCommandPool) X(DestroyCommandPool)              \
    X(AllocateCommandBuffers) X(FreeCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandBuffer)    \
    X(CmdPipelineBarrier) X(CmdCopyImageToBuffer) X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements)      \
    X(AllocateMemory) X(FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CreateFence) X(DestroyFence)    \
    X(WaitForFences) X(ResetFences) X(CreateSemaphore) X(DestroySemaphore) X(CreateImageView) X(DestroyImageView)      \
    X(CreateRenderPass) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) X(CmdBeginRenderPass)          \
    X(CmdEndRenderPass) X(CreateDescriptorPool) X(DestroyDescriptorPool) X(DeviceWaitIdle)

struct DeviceTable {
#define MOD_FIELD(name) PFN_vk##name name;
    MOD_DEVICE_FUNCTIONS(MOD_FIELD)
#undef MOD_FIELD
};

namespace mod::layer {
bool Active();                              // the runner's Vulkan instance came through the layer
std::string Shot(const std::wstring& path);  // the next presented frame as a BMP
}  // namespace mod::layer

struct IDXGISwapChain;
struct IUnknown;

namespace mod::dxgi {
void Install();  // DllMain: the runner's CreateDXGIFactory imports
int Presents();
}  // namespace mod::dxgi

namespace mod::overlay {

void SwapchainMade(IDXGISwapChain* swapchain, IUnknown* queue);  // a DXGI swapchain of the runner's, its D3D12 queue
void BeforePresent(IDXGISwapChain* swapchain);  // each DXGI present: the panel, a capture asked for
std::string Shot(const std::wstring& path);     // the next DXGI frame as a BMP
bool Active();                                  // a D3D12 swapchain is known

void SwapchainCreated(VkDevice device, VkPhysicalDevice physical, const DeviceTable& t, VkSwapchainKHR swapchain,
                      VkFormat format, VkExtent2D extent, const std::vector<VkImage>& images);
void SwapchainDestroyed(VkDevice device, const DeviceTable& t, VkSwapchainKHR swapchain);
void DeviceLost(VkDevice device);
// the panel drawn over that image, after the present's semaphores; true: wait on *done instead of them
bool Draw(VkQueue queue, uint32_t family, VkSwapchainKHR swapchain, uint32_t index, uint32_t wait_count,
          const VkSemaphore* waits, VkSemaphore* done);

}  // namespace mod::overlay
