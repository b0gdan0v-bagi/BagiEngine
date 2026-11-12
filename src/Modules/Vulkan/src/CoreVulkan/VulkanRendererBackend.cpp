#include "VulkanRendererBackend.h"

#include "imgui_vert_spv.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/MainWindow/IMainWindow.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreVulkan/VulkanRenderTarget.h>
#include <CoreVulkan/VulkanTexture.h>
#include <CoreVulkan/VulkanTextureUpload.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <Generated/VulkanRendererBackend.gen.hpp>
#include <SDL3/SDL_vulkan.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr size_t kIndicesPerTriangle = 3;

        // A minimized window skips its frames; this sleep keeps the loop from spinning at full speed meanwhile
        constexpr uint32_t kMinimizedSleepMs = 16;
        // The same after a swapchain that could not be rebuilt, so that a lasting failure does not log 60 times a second
        constexpr uint32_t kRecreateFailedSleepMs = 100;

        using namespace VulkanSwapchainPolicy;

    }  // namespace

    bool VulkanRendererBackend::Initialize(IMainWindow& window) {
        BE_PROFILE_SCOPE("Vulkan::Initialize");
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

        _window = nativeWindow;

        // The swapchain has the pixel size of the surface (more than the window's size in units on a high-density
        // display); a window that starts minimized has none yet, so the configured size stands in until the first rebuild
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(_window, &width, &height);
        if (IsMinimized(width, height)) {
            const float density = window.GetPixelDensity();
            width = static_cast<int>(std::lround(static_cast<float>(window.GetWidth()) * density));
            height = static_cast<int>(std::lround(static_cast<float>(window.GetHeight()) * density));
        }
        _requestedPixelExtent = {static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0))};

        if (!_swapchain.Initialize(_device, _surface, width, height)) {
            return false;
        }

        if (!_rectPipeline.Initialize(_device.GetDevice(), _swapchain.GetRenderPass())) {
            return false;
        }

        if (!_texturePipeline.Initialize(_device.GetDevice(), _swapchain.GetRenderPass())) {
            return false;
        }

        if (!_geometryPipeline.Initialize(_device.GetDevice(), _swapchain.GetRenderPass(), _texturePipeline.GetDescriptorSetLayout())) {
            return false;
        }

        if (!_geometryBuffer.Initialize(_device, kMaxFramesInFlight)) {
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
        Subscribe<SDLEvents::WindowEvent, &VulkanRendererBackend::OnWindowEvent>(this);
        Subscribe<RenderEvents::ImGuiInitEvent, &VulkanRendererBackend::OnImGuiInit>(this);
        Subscribe<RenderEvents::ImGuiShutdownEvent, &VulkanRendererBackend::OnImGuiShutdown>(this);
        Subscribe<RenderEvents::ImGuiNewFrameEvent, &VulkanRendererBackend::OnImGuiNewFrame>(this);
        Subscribe<RenderEvents::ImGuiRenderEvent, &VulkanRendererBackend::OnImGuiRender>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &VulkanRendererBackend::Destroy>(this);

        LOG_INFO("VulkanRendererBackend: Initialized successfully");
        return true;
    }

    void VulkanRendererBackend::WaitIdle() {
        BE_PROFILE_SCOPE("Vulkan::WaitIdle");
        if (_device.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(_device.GetDevice());
        }
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

        // The white texel's descriptor set belongs to the texture pipeline's pool, so it goes first
        _whiteTexture = {};
        _geometryBuffer.Destroy(vkDevice);
        _rectPipeline.Destroy(vkDevice);
        _texturePipeline.Destroy(vkDevice);
        _geometryPipeline.Destroy(vkDevice);
        _swapchain.Destroy(_device);

        // Surface must be destroyed before the instance
        if (_surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(_device.GetInstance(), _surface, nullptr);
            _surface = VK_NULL_HANDLE;
        }

        // Device (logical + physical + instance) cleanup
        _device.Destroy();
    }

    bool VulkanRendererBackend::RecreateSwapchain(int pixelWidth, int pixelHeight) {
        BE_PROFILE_SCOPE("Vulkan::RecreateSwapchain");
        // The old framebuffers and image views may still be in use by the frames in flight
        vkDeviceWaitIdle(_device.GetDevice());

        switch (_swapchain.Recreate(_device, _surface, pixelWidth, pixelHeight)) {
            case SwapchainRecreateResult::Done:
                _requestedPixelExtent = {static_cast<uint32_t>(pixelWidth), static_cast<uint32_t>(pixelHeight)};
                _swapchainDirty = false;
                _currentExtent = _swapchain.GetExtent();
                return true;
            case SwapchainRecreateResult::ZeroExtent:
                SDL_Delay(kMinimizedSleepMs);
                break;
            default:
                // Recreate logged the cause. The swapchain stays dirty, so the next frame tries again
                SDL_Delay(kRecreateFailedSleepMs);
                break;
        }
        _swapchainDirty = true;
        return false;
    }

    SwapchainAction VulkanRendererBackend::HandleSwapchainStatus(SwapchainStatus status, SwapchainStage stage) {
        const SwapchainAction action = ActionAfter(stage, status);
        switch (action) {
            case SwapchainAction::Proceed:
                break;
            case SwapchainAction::ProceedAndRecreateLater:
            case SwapchainAction::RecreateAndRetry:
                _swapchainDirty = true;
                break;
            default:
                LOG_ERROR("VulkanRendererBackend: {}"_format(stage == SwapchainStage::Acquire ? "acquiring a swapchain image failed, so the frame is skipped" : "presenting a swapchain image failed"));
                break;
        }
        return action;
    }

    void VulkanRendererBackend::AcquireFrame() {
        BE_PROFILE_SCOPE("Vulkan::AcquireFrame");
        if (_frameAcquired) {
            return;
        }

        // Follow the window: SDL_GetWindowSizeInPixels is the size of the surface, whatever the driver reports
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSizeInPixels(_window, &pixelWidth, &pixelHeight);
        if (IsMinimized(pixelWidth, pixelHeight)) {
            SDL_Delay(kMinimizedSleepMs);
            return;
        }
        if (NeedsRecreate(_requestedPixelExtent, pixelWidth, pixelHeight, _swapchainDirty) && !RecreateSwapchain(pixelWidth, pixelHeight)) {
            return;
        }

        VkDevice vkDevice = _device.GetDevice();
        const uint32_t frameIdx = _currentFrame;

        // Wait until the GPU finishes the previous use of this frame slot
        {
            BE_PROFILE_SCOPE("Vulkan::WaitFence");
            vkWaitForFences(vkDevice, 1, &_inFlightFences[frameIdx], VK_TRUE, UINT64_MAX);
        }

        BE_PROFILE_SCOPE("Vulkan::AcquireImage");
        SwapchainAction action = HandleSwapchainStatus(_swapchain.AcquireNextImage(_device, _imageAvailableSemaphores[frameIdx], _currentImageIndex), SwapchainStage::Acquire);
        if (action == SwapchainAction::RecreateAndRetry) {
            // A failed acquire signaled nothing, so the semaphore is free for the second try. If that is out of date
            // too, the swapchain stays dirty and the next frame tries again.
            if (!RecreateSwapchain(pixelWidth, pixelHeight)) {
                return;
            }
            action = HandleSwapchainStatus(_swapchain.AcquireNextImage(_device, _imageAvailableSemaphores[frameIdx], _currentImageIndex), SwapchainStage::Acquire);
        }
        if (action != SwapchainAction::Proceed && action != SwapchainAction::ProceedAndRecreateLater) {
            return;
        }

        vkResetFences(vkDevice, 1, &_inFlightFences[frameIdx]);

        // The fence of this slot was waited on above, so the GPU is done with the geometry of its last use
        _geometryBuffer.BeginFrame(frameIdx);

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
        BE_PROFILE_SCOPE("Vulkan::EndFrame");
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

        {
            BE_PROFILE_SCOPE("Vulkan::QueueSubmit");
            vkQueueSubmit(_device.GetGraphicsQueue(), 1, &submitInfo, _inFlightFences[frameIdx]);
        }

        _inSwapchainPass = false;
        _frameAcquired   = false;
        _frameSubmitted  = true;
    }

    void VulkanRendererBackend::Clear(const Color& color) {
        _clearColor.float32[0] = color.r / 255.0f;
        _clearColor.float32[1] = color.g / 255.0f;
        _clearColor.float32[2] = color.b / 255.0f;
        _clearColor.float32[3] = color.a / 255.0f;
    }

    void VulkanRendererBackend::Present() {
        BE_PROFILE_SCOPE("Vulkan::Present");
        // A frame that was acquired but never begun still owns a signaled semaphore: it is submitted with an empty pass
        if (_frameAcquired && !_inSwapchainPass && !_inOffscreenPass) {
            BeginFrame();
        }
        EndFrame();

        // A skipped frame (minimized, swapchain out of date) acquired no image and submitted nothing: there is
        // nothing to present, and the frame slot stays as it is
        if (!_frameSubmitted) {
            return;
        }
        _frameSubmitted = false;

        const uint32_t frameIdx = _currentFrame;
        BE_PROFILE_SCOPE("Vulkan::QueuePresent");
        HandleSwapchainStatus(_swapchain.Present(_device, _renderFinishedSemaphores[frameIdx], _currentImageIndex), SwapchainStage::Present);

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

    bool VulkanRendererBackend::ResolveScissor(VkRect2D& scissor) const {
        scissor = VkRect2D{{0, 0}, _currentExtent};
        const std::optional<Rect> clip = GetClipRect();
        if (!clip) {
            return true;
        }
        // A scissor offset cannot be negative and must stay inside the framebuffer; grow the rect to whole pixels first
        const auto clampToExtent = [](float value, uint32_t limit) { return static_cast<int32_t>(std::clamp(value, 0.0f, static_cast<float>(limit))); };
        const int32_t left = clampToExtent(std::floor(clip->x), _currentExtent.width);
        const int32_t top = clampToExtent(std::floor(clip->y), _currentExtent.height);
        const int32_t right = clampToExtent(std::ceil(clip->x + clip->w), _currentExtent.width);
        const int32_t bottom = clampToExtent(std::ceil(clip->y + clip->h), _currentExtent.height);
        if (right <= left || bottom <= top) {
            return false;
        }
        scissor = VkRect2D{{left, top}, {static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top)}};
        return true;
    }

    void VulkanRendererBackend::WarnIfBlendModeIgnored() {
        if (_blendWarned || GetBlendMode() == BlendMode::Alpha) {
            return;
        }
        _blendWarned = true;
        LOG_WARNING("VulkanRendererBackend: the blend mode {} is not implemented, so straight alpha is used"_format(EnumUtils<BlendMode>::ToString(GetBlendMode())));
    }

    void VulkanRendererBackend::DrawFilledRect(float x, float y, float w, float h, const Color& color) {
        if (!_inSwapchainPass && !_inOffscreenPass) {
            return;
        }
        WarnIfBlendModeIgnored();
        VkRect2D scissor{};
        if (!ResolveScissor(scissor)) {
            return;
        }
        const Rect rect = GetViewTransform().ToOutput(x, y, w, h);
        _rectPipeline.Draw(_commandBuffers[_currentFrame], rect.x, rect.y, rect.w, rect.h, color, _currentExtent, scissor);
        CountDraw(nullptr, kQuadTriangles);
    }

    void VulkanRendererBackend::DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) {
        if (!_inSwapchainPass && !_inOffscreenPass) {
            return;
        }
        WarnIfBlendModeIgnored();

        auto* vkTexture = dynamic_cast<VulkanTexture*>(&texture);
        if (!vkTexture || vkTexture->GetDescriptorSet() == VK_NULL_HANDLE) {
            return;
        }

        VkRect2D scissor{};
        if (!ResolveScissor(scissor)) {
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

        const Rect dst = GetViewTransform().ToOutput(dstX, dstY, dstW, dstH);

        constexpr float kColorScale = 1.0f / 255.0f;
        VulkanTexturePipeline::PushConstants pc{};
        pc.dstX    = dst.x;
        pc.dstY    = dst.y;
        pc.dstW    = dst.w;
        pc.dstH    = dst.h;
        pc.u0      = u0;
        pc.v0      = v0;
        pc.u1      = u1;
        pc.v1      = v1;
        pc.tintR   = static_cast<float>(tint.r) * kColorScale;
        pc.tintG   = static_cast<float>(tint.g) * kColorScale;
        pc.tintB   = static_cast<float>(tint.b) * kColorScale;
        pc.tintA   = static_cast<float>(tint.a) * kColorScale;
        pc.screenW = static_cast<float>(_currentExtent.width);
        pc.screenH = static_cast<float>(_currentExtent.height);

        _texturePipeline.Draw(_commandBuffers[_currentFrame], vkTexture->GetDescriptorSet(), pc, scissor);
        CountDraw(&texture, kQuadTriangles);
    }

    VkDescriptorSet VulkanRendererBackend::GetWhiteTextureDescriptorSet() {
        if (!_whiteTexture) {
            constexpr uint8_t kWhiteTexel[4] = {255, 255, 255, 255};
            _whiteTexture = CreateTexture(kWhiteTexel, 1, 1);
        }
        const auto* white = static_cast<const VulkanTexture*>(_whiteTexture.Get());
        return white ? white->GetDescriptorSet() : VK_NULL_HANDLE;
    }

    void VulkanRendererBackend::DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY) {
        if (!_inSwapchainPass && !_inOffscreenPass) {
            return;
        }
        WarnIfBlendModeIgnored();
        if (vertices.empty() || indices.size() < kIndicesPerTriangle) {
            return;
        }

        // A texture of another backend draws nothing, as in DrawTexture()
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        if (texture) {
            const auto* vkTexture = dynamic_cast<const VulkanTexture*>(texture);
            descriptorSet = vkTexture ? vkTexture->GetDescriptorSet() : VK_NULL_HANDLE;
        } else {
            descriptorSet = GetWhiteTextureDescriptorSet();
        }
        if (descriptorSet == VK_NULL_HANDLE) {
            return;
        }

        const int vertexCount = static_cast<int>(vertices.size());
        for (const int index : indices) {
            if (index < 0 || index >= vertexCount) {
                LOG_ERROR("VulkanRendererBackend: DrawGeometry got the index {} for {} vertices, so nothing is drawn"_format(index, vertexCount));
                return;
            }
        }

        VkRect2D scissor{};
        if (!ResolveScissor(scissor)) {
            return;
        }

        // Only whole triangles are drawn, as in the SDL backend
        const size_t indexCount = indices.size() - indices.size() % kIndicesPerTriangle;
        VulkanGeometryBuffer::Allocation allocation;
        if (!_geometryBuffer.Allocate(_currentFrame, vertices.size() * sizeof(VulkanGeometryPipeline::Vertex), indexCount * sizeof(uint32_t), allocation)) {
            return;
        }

        const ViewTransform& view = GetViewTransform();
        auto* out = static_cast<VulkanGeometryPipeline::Vertex*>(allocation.vertices);
        for (size_t i = 0; i < vertices.size(); ++i) {
            const GeometryVertex& vertex = vertices[i];
            const float x = (vertex.x + translateX) * view.scale + view.offsetX;
            const float y = (vertex.y + translateY) * view.scale + view.offsetY;
            out[i] = {x, y, vertex.color.r, vertex.color.g, vertex.color.b, vertex.color.a, vertex.u, vertex.v};
        }
        auto* outIndices = static_cast<uint32_t*>(allocation.indices);
        for (size_t i = 0; i < indexCount; ++i) {
            outIndices[i] = static_cast<uint32_t>(indices[i]);
        }

        _geometryPipeline.Draw(_commandBuffers[_currentFrame], descriptorSet, allocation.buffer, allocation.vertexOffset, allocation.indexOffset, static_cast<uint32_t>(indexCount), _currentExtent,
                               scissor);
        CountDraw(texture, indexCount / kIndicesPerTriangle);
    }

    IntrusivePtr<ITexture> VulkanRendererBackend::CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) {
        BE_PROFILE_SCOPE("Vulkan::CreateTexture");
        if (!rgba || width == 0 || height == 0) {
            return {};
        }
        auto texture = VulkanTextureUpload::Upload(*this, "<memory>"_intern, rgba, width, height);
        if (texture->GetState() != ResourceState::Loaded) {
            return {};
        }
        return texture;
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
        BE_PROFILE_SCOPE("Vulkan::SetRenderTarget");
        // No open frame (minimized, swapchain out of date): the offscreen draws are skipped like everything else
        if (!_frameAcquired) {
            return;
        }
        ASSERT(!_inOffscreenPass && !_inSwapchainPass);
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
        BE_PROFILE_SCOPE("Vulkan::UnsetRenderTarget");
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

    void VulkanRendererBackend::OnWindowEvent(const SDLEvents::WindowEvent& event) {
        // The per-frame comparison of the pixel size is the safety net for a driver that reports nothing
        if (event.window.type == SDL_EVENT_WINDOW_RESIZED || event.window.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            _swapchainDirty = true;
        }
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

        // ImGui's vertex colours are sRGB values and the swapchain is an sRGB attachment, so the vertex shader turns
        // them linear. The backend has one shader pair for the main window and the floating ones, so those ask for an
        // sRGB swapchain too: the backend reads the format from PipelineRenderingCreateInfo even without dynamic
        // rendering (UseDynamicRendering stays false, the render pass is made from the chosen format).
        const ImGuiColorMode colorMode = ChooseImGuiColorMode(_swapchain.GetImageFormat());
        if (colorMode == ImGuiColorMode::Linearize) {
            vulkanInfo.CustomShaderVertCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            vulkanInfo.CustomShaderVertCreateInfo.codeSize = kImGuiVertSpvSize;
            vulkanInfo.CustomShaderVertCreateInfo.pCode = kImGuiVertSpv;
            _imguiViewportFormat = ImGuiViewportFormat(colorMode);
            vulkanInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            vulkanInfo.PipelineInfoForViewports.PipelineRenderingCreateInfo.pColorAttachmentFormats = &_imguiViewportFormat;
        }
        LOG_INFO("VulkanRendererBackend: ImGui colour mode {}"_format(EnumUtils<ImGuiColorMode>::ToString(colorMode)));
        ImGui_ImplVulkan_Init(&vulkanInfo);
    }

    void VulkanRendererBackend::OnImGuiShutdown() {
        ImGui_ImplVulkan_Shutdown();
    }

    void VulkanRendererBackend::OnImGuiNewFrame() {
        BE_PROFILE_SCOPE("Vulkan::ImGuiNewFrame");
        ImGui_ImplSDL3_NewFrame();
        ImGui_ImplVulkan_NewFrame();
    }

    void VulkanRendererBackend::OnImGuiRender() {
        BE_PROFILE_SCOPE("Vulkan::ImGuiRender");
        // Only into an open swapchain pass: a skipped frame has no command buffer to record into
        if (!_inSwapchainPass) {
            return;
        }
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandBuffer());
    }

}  // namespace BECore
