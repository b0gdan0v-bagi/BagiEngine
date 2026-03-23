#include "VulkanRendererBackend.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreVulkan/VulkanRenderTarget.h>
#include <CoreVulkan/VulkanTexture.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/VulkanRendererBackend.gen.hpp>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

namespace BECore {

    bool VulkanRendererBackend::Initialize(IMainWindow& window) {
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(&window);
        if (!sdlWindow) {
            LOG_ERROR("VulkanRendererBackend: IMainWindow is not a SDLMainWindow");
            return false;
        }

        SDL_Window* nativeWindow = sdlWindow->GetSDLWindow();
        if (!nativeWindow) {
            LOG_ERROR("VulkanRendererBackend: SDL_Window is null");
            return false;
        }

        // Verify the window was created with SDL_WINDOW_VULKAN flag
        const SDL_WindowFlags windowFlags = SDL_GetWindowFlags(nativeWindow);
        if (!(windowFlags & SDL_WINDOW_VULKAN)) {
            LOG_ERROR("VulkanRendererBackend: SDL_Window was not created with SDL_WINDOW_VULKAN flag. "
                      "Add windowFlags=\"SDL_WINDOW_VULKAN\" to SDLWindowConfig.xml");
            return false;
        }

        // Step 1: Create VkInstance (needed before surface creation)
        if (!_device.InitializeInstance(nativeWindow)) {
            return false;
        }

        // Step 2: Create VkSurfaceKHR via SDL (platform-agnostic, needs a valid instance)
        if (!SDL_Vulkan_CreateSurface(nativeWindow, _device.GetInstance(), nullptr, &_surface)) {
            LOG_ERROR("VulkanRendererBackend: SDL_Vulkan_CreateSurface failed");
            return false;
        }

        // Step 3: Pick physical device and create logical device (needs surface for queue detection)
        if (!_device.InitializeDevice(_surface)) {
            return false;
        }

        const int width = window.GetWidth();
        const int height = window.GetHeight();

        if (!_swapchain.Initialize(_device, _surface, width, height)) {
            return false;
        }

        if (!_rectPipeline.Initialize(_device.GetDevice(), _swapchain.GetRenderPass())) {
            return false;
        }

        if (!_texturePipeline.Initialize(_device.GetDevice(), _swapchain.GetRenderPass())) {
            return false;
        }

        if (!CreateCommandPool()) {
            return false;
        }
        if (!CreateCommandBuffers()) {
            return false;
        }
        if (!CreateSyncObjects()) {
            return false;
        }

        Subscribe<RenderEvents::NewFrameEvent, &VulkanRendererBackend::OnNewFrame>(this);
        Subscribe<RenderEvents::SetRenderDrawColorEvent, &VulkanRendererBackend::OnSetRenderDrawColor>(this);
        Subscribe<RenderEvents::RenderClearEvent, &VulkanRendererBackend::OnRenderClear>(this);
        Subscribe<RenderEvents::RenderPresentEvent, &VulkanRendererBackend::OnRenderPresent>(this);
        Subscribe<RenderEvents::ImGuiInitEvent, &VulkanRendererBackend::OnImGuiInit>(this);
        Subscribe<RenderEvents::ImGuiShutdownEvent, &VulkanRendererBackend::OnImGuiShutdown>(this);
        Subscribe<RenderEvents::ImGuiNewFrameEvent, &VulkanRendererBackend::OnImGuiNewFrame>(this);
        Subscribe<RenderEvents::ImGuiRenderEvent, &VulkanRendererBackend::OnImGuiRender>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &VulkanRendererBackend::Destroy>(this);

        LOG_INFO("VulkanRendererBackend: Initialized successfully");
        return true;
    }

    void VulkanRendererBackend::Destroy() {
        VkDevice vkDevice = _device.GetDevice();
        if (vkDevice == VK_NULL_HANDLE) {
            return;
        }

        vkDeviceWaitIdle(vkDevice);

        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            vkDestroySemaphore(vkDevice, _imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(vkDevice, _renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(vkDevice, _inFlightFences[i], nullptr);
        }
        _imageAvailableSemaphores.clear();
        _renderFinishedSemaphores.clear();
        _inFlightFences.clear();

        if (_commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(vkDevice, _commandPool, nullptr);
            _commandPool = VK_NULL_HANDLE;
        }

        _rectPipeline.Destroy(vkDevice);
        _texturePipeline.Destroy(vkDevice);
        _swapchain.Destroy(_device);

        // Surface must be destroyed before the instance
        if (_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(_device.GetInstance(), _surface, nullptr);
            _surface = VK_NULL_HANDLE;
        }

        // Device (logical + physical + instance) cleanup
        _device.Destroy();
    }

    void VulkanRendererBackend::AcquireFrame() {
        if (_frameAcquired) {
            return;
        }

        VkDevice vkDevice = _device.GetDevice();
        const uint32_t frameIdx = _currentFrame;

        // Wait until the GPU finishes the previous use of this frame slot
        vkWaitForFences(vkDevice, 1, &_inFlightFences[frameIdx], VK_TRUE, UINT64_MAX);

        if (!_swapchain.AcquireNextImage(_device, _imageAvailableSemaphores[frameIdx], _currentImageIndex)) {
            return;  // swapchain out-of-date (TODO: recreate on resize)
        }

        vkResetFences(vkDevice, 1, &_inFlightFences[frameIdx]);

        VkCommandBuffer cmd = _commandBuffers[frameIdx];
        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkBeginCommandBuffer(cmd, &beginInfo);

        _frameAcquired = true;
    }

    void VulkanRendererBackend::BeginFrame() {
        // Begin the swapchain render pass. AcquireFrame() must have been called first.
        if (!_frameAcquired || _inSwapchainPass) {
            return;
        }

        VkCommandBuffer cmd = _commandBuffers[_currentFrame];

        VkClearValue clearValue{};
        clearValue.color = _clearColor;

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass  = _swapchain.GetRenderPass();
        rpBegin.framebuffer = _swapchain.GetFramebuffer(_currentImageIndex);
        rpBegin.renderArea  = {{0, 0}, _swapchain.GetExtent()};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues    = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        _currentExtent   = _swapchain.GetExtent();
        _inSwapchainPass = true;
    }

    void VulkanRendererBackend::EndFrame() {
        if (!_inSwapchainPass) {
            return;
        }

        const uint32_t frameIdx = _currentFrame;
        VkCommandBuffer cmd = _commandBuffers[frameIdx];

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);

        // Submit the command buffer
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores    = &_imageAvailableSemaphores[frameIdx];
        submitInfo.pWaitDstStageMask  = &waitStage;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers    = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores    = &_renderFinishedSemaphores[frameIdx];

        vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, _inFlightFences[frameIdx]);

        _inSwapchainPass = false;
        _frameAcquired   = false;
    }

    void VulkanRendererBackend::Clear(const Color& color) {
        _clearColor.float32[0] = color.r / 255.0f;
        _clearColor.float32[1] = color.g / 255.0f;
        _clearColor.float32[2] = color.b / 255.0f;
        _clearColor.float32[3] = color.a / 255.0f;
    }

    void VulkanRendererBackend::Present() {
        EndFrame();

        const uint32_t frameIdx = _currentFrame;
        _swapchain.Present(_device, _renderFinishedSemaphores[frameIdx], _currentImageIndex);

        _currentFrame = (_currentFrame + 1) % kMaxFramesInFlight;
    }

    VkCommandBuffer VulkanRendererBackend::GetCurrentCommandBuffer() const {
        return _commandBuffers[_currentFrame];
    }

    bool VulkanRendererBackend::CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = _device.GetQueueFamilies().graphicsFamily;

        return vkCreateCommandPool(_device.GetDevice(), &poolInfo, nullptr, &_commandPool) == VK_SUCCESS;
    }

    bool VulkanRendererBackend::CreateCommandBuffers() {
        _commandBuffers.resize(kMaxFramesInFlight);

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = _commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(_commandBuffers.size());

        return vkAllocateCommandBuffers(_device.GetDevice(), &allocInfo, _commandBuffers.data()) == VK_SUCCESS;
    }

    bool VulkanRendererBackend::CreateSyncObjects() {
        _imageAvailableSemaphores.resize(kMaxFramesInFlight);
        _renderFinishedSemaphores.resize(kMaxFramesInFlight);
        _inFlightFences.resize(kMaxFramesInFlight);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        // Create the fence in signaled state so the first frame doesn't wait forever
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < kMaxFramesInFlight; ++i) {
            if (vkCreateSemaphore(_device.GetDevice(), &semInfo, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(_device.GetDevice(), &semInfo, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(_device.GetDevice(), &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    // =========================================================================
    // Event handlers (translate engine render events to Vulkan calls)
    // =========================================================================

    void VulkanRendererBackend::DrawFilledRect(float x, float y, float w, float h, const Color& color) {
        if (!_inSwapchainPass && !_inOffscreenPass) {
            return;
        }
        _rectPipeline.Draw(_commandBuffers[_currentFrame], x, y, w, h, color, _currentExtent);
    }

    void VulkanRendererBackend::DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH) {
        if (!_inSwapchainPass && !_inOffscreenPass) {
            return;
        }

        auto* vkTexture = dynamic_cast<VulkanTexture*>(&texture);
        if (!vkTexture || vkTexture->GetDescriptorSet() == VK_NULL_HANDLE) {
            return;
        }

        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (srcRect && !srcRect->IsEmpty()) {
            const float texW = vkTexture->GetWidth();
            const float texH = vkTexture->GetHeight();
            if (texW > 0.0f && texH > 0.0f) {
                u0 = srcRect->x / texW;
                v0 = srcRect->y / texH;
                u1 = (srcRect->x + srcRect->w) / texW;
                v1 = (srcRect->y + srcRect->h) / texH;
            }
        }

        VulkanTexturePipeline::PushConstants pc{};
        pc.dstX    = dstX;
        pc.dstY    = dstY;
        pc.dstW    = dstW;
        pc.dstH    = dstH;
        pc.u0      = u0;
        pc.v0      = v0;
        pc.u1      = u1;
        pc.v1      = v1;
        pc.screenW = static_cast<float>(_currentExtent.width);
        pc.screenH = static_cast<float>(_currentExtent.height);

        _texturePipeline.Draw(_commandBuffers[_currentFrame], vkTexture->GetDescriptorSet(), pc, _currentExtent);
    }

    IntrusivePtr<IRenderTarget> VulkanRendererBackend::CreateRenderTarget(uint32_t width, uint32_t height) {
        auto target = New<VulkanRenderTarget>();
        if (!target->Initialize(_device.GetDevice(), _device.GetPhysicalDevice(),
                                 _swapchain.GetImageFormat(), width, height)) {
            LOG_ERROR("VulkanRendererBackend: CreateRenderTarget failed");
            return {};
        }
        return target;
    }

    void VulkanRendererBackend::SetRenderTarget(IRenderTarget* target) {
        ASSERT(!_inOffscreenPass && !_inSwapchainPass && _frameAcquired);
        auto* vkTarget = static_cast<VulkanRenderTarget*>(target);

        VkCommandBuffer cmd = _commandBuffers[_currentFrame];

        VkClearValue clearValue{};
        clearValue.color = _clearColor;

        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass      = vkTarget->GetRenderPass();
        rpInfo.framebuffer     = vkTarget->GetFramebuffer();
        rpInfo.renderArea      = {{0, 0}, vkTarget->GetExtent()};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues    = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        const VkExtent2D ext = vkTarget->GetExtent();

        VkViewport vp{};
        vp.x        = 0.0f;
        vp.y        = 0.0f;
        vp.width    = static_cast<float>(ext.width);
        vp.height   = static_cast<float>(ext.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor{{0, 0}, ext};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        _currentExtent      = ext;
        _inOffscreenPass    = true;
        _lastOffscreenImage = vkTarget->GetImage();
    }

    void VulkanRendererBackend::UnsetRenderTarget() {
        if (!_inOffscreenPass) {
            return;
        }

        VkCommandBuffer cmd = _commandBuffers[_currentFrame];
        vkCmdEndRenderPass(cmd);

        // The render pass transitions the image to SHADER_READ_ONLY_OPTIMAL via finalLayout.
        // Add an explicit pipeline barrier so the swapchain pass's fragment shader can read it.
        VkImageMemoryBarrier barrier{};
        barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        barrier.image = _lastOffscreenImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask               = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask               = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        _inOffscreenPass   = false;
        _lastOffscreenImage = VK_NULL_HANDLE;
    }

    void VulkanRendererBackend::OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event) {
        Clear(event.color);
    }

    void VulkanRendererBackend::OnNewFrame() {
        AcquireFrame();
    }

    void VulkanRendererBackend::OnRenderClear() {
        BeginFrame();
    }

    void VulkanRendererBackend::OnRenderPresent() {
        Present();
    }

    void VulkanRendererBackend::OnImGuiInit() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return;
        }
        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(window.Get());
        if (!sdlWindow) {
            return;
        }
        ImGui_ImplSDL3_InitForVulkan(sdlWindow->GetSDLWindow());

        ImGui_ImplVulkan_InitInfo vulkanInfo{};
        vulkanInfo.ApiVersion = VK_API_VERSION_1_0;
        vulkanInfo.Instance = _device.GetInstance();
        vulkanInfo.PhysicalDevice = _device.GetPhysicalDevice();
        vulkanInfo.Device = _device.GetDevice();
        vulkanInfo.QueueFamily = _device.GetQueueFamilies().graphicsFamily;
        vulkanInfo.Queue = _device.GetGraphicsQueue();
        vulkanInfo.MinImageCount = kMaxFramesInFlight;
        vulkanInfo.ImageCount = kMaxFramesInFlight;
        vulkanInfo.DescriptorPoolSize = 1000;
        vulkanInfo.PipelineInfoMain.RenderPass = _swapchain.GetRenderPass();
        vulkanInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        ImGui_ImplVulkan_Init(&vulkanInfo);
    }

    void VulkanRendererBackend::OnImGuiShutdown() {
        ImGui_ImplVulkan_Shutdown();
    }

    void VulkanRendererBackend::OnImGuiNewFrame() {
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplVulkan_NewFrame();
    }

    void VulkanRendererBackend::OnImGuiRender() {
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandBuffer());
    }

}  // namespace BECore
