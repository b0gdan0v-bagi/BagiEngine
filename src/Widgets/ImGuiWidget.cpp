#include "ImGuiWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <CoreSDL/SDLMainWindow.h>
#include <CoreSDL/SDLRendererBackend.h>

#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>
#include <CoreSDL/SDLEvents.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#if defined(IMGUI_VULKAN_AVAILABLE)
#include <CoreVulkan/VulkanRendererBackend.h>
#include <backends/imgui_impl_vulkan.h>
#endif

#include <Generated/ImGuiWidget.gen.hpp>

namespace BECore {

    bool ImGuiWidget::Initialize(IDeserializer& /*deserializer*/) {
        if (_isInitialized) {
            return true;
        }

        SDL_Window* window = GetSDLWindow();
        if (!window) {
            return false;
        }

        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer) {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

        ImGui::StyleColorsDark();

        if (auto* sdl = renderer->Cast<SDLRendererBackend>()) {
            ImGui_ImplSDL3_InitForSDLRenderer(window, sdl->GetSDLRenderer());
            ImGui_ImplSDLRenderer3_Init(sdl->GetSDLRenderer());
        }
#if defined(IMGUI_VULKAN_AVAILABLE)
        else if (auto* vk = renderer->Cast<VulkanRendererBackend>()) {
            ImGui_ImplSDL3_InitForVulkan(window);

            ImGui_ImplVulkan_InitInfo vulkanInfo{};
            vulkanInfo.ApiVersion      = VK_API_VERSION_1_0;
            vulkanInfo.Instance        = vk->GetVkInstance();
            vulkanInfo.PhysicalDevice  = vk->GetVkPhysicalDevice();
            vulkanInfo.Device          = vk->GetVkDevice();
            vulkanInfo.QueueFamily     = vk->GetGraphicsQueueFamily();
            vulkanInfo.Queue           = vk->GetVkGraphicsQueue();
            vulkanInfo.MinImageCount   = vk->GetMinImageCount();
            vulkanInfo.ImageCount      = vk->GetMinImageCount();
            vulkanInfo.PipelineInfoMain.RenderPass  = vk->GetVkRenderPass();
            vulkanInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
            ImGui_ImplVulkan_Init(&vulkanInfo);
        }
#endif

        Subscribe<SDLEvents::SDLEventWrapper, &ImGuiWidget::OnSDLEvent>(this);
        Subscribe<RenderEvents::NewFrameEvent, &ImGuiWidget::OnNewFrame>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &ImGuiWidget::Destroy>(this);

        _isInitialized = true;
        return true;
    }

    void ImGuiWidget::Draw() {
        if (!_isInitialized) {
            return;
        }

        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer) {
            return;
        }

        ImGui::Render();

        if (renderer->Is<SDLRendererBackend>()) {
            auto* sdl = renderer->Cast<SDLRendererBackend>();
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), sdl->GetSDLRenderer());
        }
#if defined(IMGUI_VULKAN_AVAILABLE)
        else if (renderer->Is<VulkanRendererBackend>()) {
            auto* vk = renderer->Cast<VulkanRendererBackend>();
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), vk->GetCurrentCommandBuffer());
        }
#endif
    }

    void ImGuiWidget::Update() {
        if (!_isInitialized) {
            return;
        }

        if (ImGui::Begin("Debug Widget")) {
            if (ImGui::Button("Quit")) {
                ApplicationEvents::QuitEvent::Emit();
            }
        }
        ImGui::End();
    }

    SDL_Window* ImGuiWidget::GetSDLWindow() {
        const auto& window = CoreManager::GetMainWindow();
        if (!window) {
            return nullptr;
        }

        auto* sdlWindow = dynamic_cast<SDLMainWindow*>(window.Get());
        if (!sdlWindow) {
            return nullptr;
        }
        return sdlWindow->GetSDLWindow();
    }

    void ImGuiWidget::Destroy() {
        if (!_isInitialized) {
            return;
        }

        const auto& renderer = CoreManager::GetRenderer();
        if (renderer) {
            if (renderer->Is<SDLRendererBackend>()) {
                ImGui_ImplSDLRenderer3_Shutdown();
            }
#if defined(IMGUI_VULKAN_AVAILABLE)
            else if (renderer->Is<VulkanRendererBackend>()) {
                ImGui_ImplVulkan_Shutdown();
            }
#endif
        }

        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();

        _isInitialized = false;
    }

    void ImGuiWidget::OnNewFrame() const {
        if (!_isInitialized) {
            return;
        }

        const auto& renderer = CoreManager::GetRenderer();
        if (!renderer) {
            return;
        }

        if (renderer->Is<SDLRendererBackend>()) {
            ImGui_ImplSDL3_NewFrame();
            ImGui_ImplSDLRenderer3_NewFrame();
        }
#if defined(IMGUI_VULKAN_AVAILABLE)
        else if (renderer->Is<VulkanRendererBackend>()) {
            ImGui_ImplSDL3_NewFrame();
            ImGui_ImplVulkan_NewFrame();
        }
#endif

        ImGui::NewFrame();
    }

    void ImGuiWidget::OnSDLEvent(const SDLEvents::SDLEventWrapper& event) const {
        if (!_isInitialized) {
            return;
        }

        ImGui_ImplSDL3_ProcessEvent(&event.event);
    }

}  // namespace BECore
