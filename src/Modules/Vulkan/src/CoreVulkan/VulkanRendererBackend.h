#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/IRenderTarget.h>
#include <CoreVulkan/VulkanDevice.h>
#include <CoreVulkan/VulkanRectPipeline.h>
#include <CoreVulkan/VulkanSwapchain.h>
#include <CoreVulkan/VulkanTexturePipeline.h>
#include <EASTL/vector.h>
#include <vulkan/vulkan.h>

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
        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH) override;

        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) override;
        void SetRenderTarget(IRenderTarget* target) override;
        void UnsetRenderTarget() override;

        // Vulkan handles exposed for imgui_impl_vulkan integration
        VkInstance GetVkInstance() const {
            return _device.GetInstance();
        }
        VkPhysicalDevice GetVkPhysicalDevice() const {
            return _device.GetPhysicalDevice();
        }
        VkDevice GetVkDevice() const {
            return _device.GetDevice();
        }
        VkQueue GetVkGraphicsQueue() const {
            return _device.GetGraphicsQueue();
        }
        VkRenderPass GetVkRenderPass() const {
            return _swapchain.GetRenderPass();
        }
        uint32_t GetGraphicsQueueFamily() const {
            return _device.GetQueueFamilies().graphicsFamily;
        }
        uint32_t GetCurrentImageIndex() const {
            return _currentImageIndex;
        }
        VkCommandBuffer GetCurrentCommandBuffer() const;
        uint32_t GetMinImageCount() const {
            return kMaxFramesInFlight;
        }

        VkCommandPool GetCommandPool() const {
            return _commandPool;
        }
        VulkanTexturePipeline& GetTexturePipeline() {
            return _texturePipeline;
        }
        const VulkanDevice& GetVulkanDevice() const {
            return _device;
        }

    private:
        bool CreateCommandPool();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();

        // Acquires the next swapchain image and opens the command buffer.
        // Called from OnNewFrame() — BEFORE the swapchain render pass begins so that
        // ViewportWidget can insert an offscreen render pass first.
        void AcquireFrame();

        void OnNewFrame();
        void OnRenderClear();
        void OnRenderPresent();
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        VulkanDevice _device;
        VulkanSwapchain _swapchain;
        VulkanRectPipeline _rectPipeline;
        VulkanTexturePipeline _texturePipeline;

        VkSurfaceKHR _surface = VK_NULL_HANDLE;

        VkCommandPool _commandPool = VK_NULL_HANDLE;
        eastl::vector<VkCommandBuffer> _commandBuffers;

        // One set of sync objects per in-flight frame
        eastl::vector<VkSemaphore> _imageAvailableSemaphores;
        eastl::vector<VkSemaphore> _renderFinishedSemaphores;
        eastl::vector<VkFence> _inFlightFences;

        uint32_t _currentFrame = 0;
        uint32_t _currentImageIndex = 0;

        // true when the command buffer is open (frame acquired, not yet presented)
        bool _frameAcquired = false;
        // true when inside the swapchain render pass (between OnRenderClear and OnRenderPresent)
        bool _inSwapchainPass = false;
        // true when inside an offscreen render pass (between SetRenderTarget/UnsetRenderTarget)
        bool _inOffscreenPass = false;

        // Current render extent — offscreen target size or swapchain extent depending on active pass
        VkExtent2D _currentExtent = {};

        // Image handle of the current offscreen target (needed for the barrier in UnsetRenderTarget)
        VkImage _lastOffscreenImage = VK_NULL_HANDLE;

        // Clear color set by SetRenderDrawColorEvent (used for the swapchain pass)
        VkClearColorValue _clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
    };

}  // namespace BECore
