#pragma once

#include <cstdint>

namespace BECore {

    // Offscreen render target whose result can be displayed as an ImGui image.
    // Obtained from IRenderer::CreateRenderTarget(). Reference-counted via IntrusivePtr.
    class IRenderTarget : public RefCounted {
    public:
        IRenderTarget() = default;
        ~IRenderTarget() override = default;

        virtual uint32_t GetWidth()  const = 0;
        virtual uint32_t GetHeight() const = 0;

        // Returns the native texture handle cast to ImTextureID.
        // For Vulkan: VkDescriptorSet from ImGui_ImplVulkan_AddTexture().
        // For SDL:    SDL_Texture* cast to ImTextureID.
        virtual void* GetImGuiTextureId() const = 0;
    };

}  // namespace BECore
