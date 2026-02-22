#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <CoreVulkan/VulkanDevice.h>
#include <CoreVulkan/VulkanSwapchain.h>

#include <vulkan/vulkan.h>
#include <EASTL/vector.h>

namespace BECore {

    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }

    /**
     * Vulkan renderer backend implementing IRenderer.
     *
     * Frame lifecycle:
     *   RenderClearEvent  -> BeginFrame(): acquire image, begin command buffer + render pass
     *   [widget Draw()]   -> widgets record commands into the active render pass
     *   RenderPresentEvent -> EndFrame(): end render pass + command buffer, submit, present
     *
     * Max number of frames processed simultaneously (in-flight)
     */
    class VulkanRendererBackend : public IRenderer {
        BE_CLASS(VulkanRendererBackend)

        static constexpr int kMaxFramesInFlight = 2;

    public:
        VulkanRendererBackend() = default;
        ~VulkanRendererBackend() override = default;

        bool Initialize(IMainWindow& window) override;
        void Destroy() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Clear(const Color& color) override;
        void Present() override;

        // Vulkan handles exposed for imgui_impl_vulkan integration
        VkInstance       GetVkInstance()      const { return _device.GetInstance(); }
        VkPhysicalDevice GetVkPhysicalDevice() const { return _device.GetPhysicalDevice(); }
        VkDevice         GetVkDevice()        const { return _device.GetDevice(); }
        VkQueue          GetVkGraphicsQueue() const { return _device.GetGraphicsQueue(); }
        VkRenderPass     GetVkRenderPass()       const { return _swapchain.GetRenderPass(); }
        uint32_t         GetGraphicsQueueFamily() const { return _device.GetQueueFamilies().graphicsFamily; }
        uint32_t         GetCurrentImageIndex()   const { return _currentImageIndex; }
        VkCommandBuffer  GetCurrentCommandBuffer() const;
        uint32_t         GetMinImageCount()        const { return kMaxFramesInFlight; }

    private:
        bool CreateCommandPool();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();

        void OnRenderClear();
        void OnRenderPresent();
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        VulkanDevice    _device;
        VulkanSwapchain _swapchain;

        VkSurfaceKHR _surface = VK_NULL_HANDLE;

        VkCommandPool                  _commandPool = VK_NULL_HANDLE;
        eastl::vector<VkCommandBuffer> _commandBuffers;

        // One set of sync objects per in-flight frame
        eastl::vector<VkSemaphore> _imageAvailableSemaphores;
        eastl::vector<VkSemaphore> _renderFinishedSemaphores;
        eastl::vector<VkFence>     _inFlightFences;

        uint32_t _currentFrame      = 0;
        uint32_t _currentImageIndex = 0;
        bool     _frameActive       = false;

        // Clear color set by SetRenderDrawColorEvent
        VkClearColorValue _clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
    };

}  // namespace BECore
