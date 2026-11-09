#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/IRenderTarget.h>
#include <CoreVulkan/VulkanDevice.h>
#include <CoreVulkan/VulkanGeometryBuffer.h>
#include <CoreVulkan/VulkanGeometryPipeline.h>
#include <CoreVulkan/VulkanRectPipeline.h>
#include <CoreVulkan/VulkanSwapchain.h>
#include <CoreVulkan/VulkanTexturePipeline.h>
#include <EASTL/vector.h>
#include <vulkan/vulkan.h>

struct SDL_Window;

namespace BECore {

    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }

    namespace SDLEvents {
        struct WindowEvent;
    }

    /**
     * Vulkan renderer backend implementing IRenderer.
     *
     * Frame lifecycle:
     *   NewFrameEvent      -> AcquireFrame(): follow the window (rebuild the swapchain when its pixel size changed),
     *                         acquire the image, open the command buffer; a minimized window skips the frame
     *   RenderClearEvent   -> BeginFrame(): begin the swapchain render pass
     *   [widget Draw()]    -> widgets record commands into the active render pass
     *   RenderPresentEvent -> Present(): end render pass + command buffer, submit, present
     * A skipped frame records nothing: every draw, SetRenderTarget and the ImGui render check that a frame is open.
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
        void WaitIdle() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Clear(const Color& color) override;
        void Present() override;
        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override;
        using IRenderer::DrawTexture;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) override;
        void DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY) override;
        IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override;

        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) override;
        void SetRenderTarget(IRenderTarget* target) override;
        void UnsetRenderTarget() override;

        float GetOutputWidth() const override {
            return static_cast<float>(_currentExtent.width);
        }
        float GetOutputHeight() const override {
            return static_cast<float>(_currentExtent.height);
        }

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
        // The scissor of the next draw: the clip rect cut to the current extent, or the whole extent when nothing is
        // clipped. False when the clip leaves nothing visible, so the draw is skipped.
        bool ResolveScissor(VkRect2D& scissor) const;

        // The descriptor set of a draw that has no texture: one white texel, made on first use. Null when it cannot be made.
        VkDescriptorSet GetWhiteTextureDescriptorSet();

        bool CreateCommandPool();
        bool CreateCommandBuffers();
        bool CreateSyncObjects();

        // Acquires the next swapchain image and opens the command buffer. Rebuilds the swapchain first when the
        // window's pixel size is not the one it was made for; a minimized window skips the frame (and sleeps a
        // little, so that the loop does not spin).
        // Called from OnNewFrame() — BEFORE the swapchain render pass begins so that
        // ViewportWidget can insert an offscreen render pass first.
        void AcquireFrame();

        // Waits for the GPU and rebuilds the swapchain for the pixel size. True when the new swapchain is in place;
        // false for a size of zero (minimized) or a failure, and then the swapchain is still marked dirty.
        bool RecreateSwapchain(int pixelWidth, int pixelHeight);

        // The one place that reads the status of an acquire or a present: marks the swapchain dirty when it must be
        // rebuilt, logs a failure, and returns what the frame does next (VulkanSwapchainPolicy::ActionAfter)
        VulkanSwapchainPolicy::SwapchainAction HandleSwapchainStatus(SwapchainStatus status, VulkanSwapchainPolicy::SwapchainStage stage);

        void OnNewFrame();
        void OnRenderClear();
        void OnRenderPresent();
        void OnWindowEvent(const SDLEvents::WindowEvent& event);
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        // Draws take only straight alpha: logs once, the first time a draw comes with another blend mode, then draws as usual
        void WarnIfBlendModeIgnored();

        VulkanDevice _device;
        VulkanSwapchain _swapchain;
        VulkanRectPipeline _rectPipeline;
        VulkanTexturePipeline _texturePipeline;
        VulkanGeometryPipeline _geometryPipeline;
        // Vertices and indices of DrawGeometry(), a set per frame in flight (rewound in AcquireFrame())
        VulkanGeometryBuffer _geometryBuffer;
        // Freed in Destroy(), while the descriptor pool of the texture pipeline is still there
        IntrusivePtr<ITexture> _whiteTexture;

        VkSurfaceKHR _surface = VK_NULL_HANDLE;

        // The window the surface belongs to (owned by SDLMainWindow, not destroyed here); its pixel size is read every frame
        SDL_Window* _window = nullptr;
        // The pixel size the swapchain was last created for: what the window is compared with, not the extent the
        // surface gave back (they may differ, and the swapchain must not be rebuilt on every frame because of it)
        VkExtent2D _requestedPixelExtent = {};
        // The swapchain must be rebuilt before the next frame (a window event, a suboptimal or out-of-date result)
        bool _swapchainDirty = false;
        // The warning about a blend mode other than Alpha was logged
        bool _blendWarned = false;

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
        // true when the command buffer of the frame was submitted and the image waits to be presented
        bool _frameSubmitted = false;
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
