#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <cstdint>

namespace BECore {

    // Offscreen render target whose result is shown as an ImGui image. Obtained from IRenderer::CreateRenderTarget().
    class IRenderTarget : public RefCounted {
    public:
        IRenderTarget() = default;
        ~IRenderTarget() override = default;

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;

        // The native handle as an ImTextureID: a VkDescriptorSet (ImGui_ImplVulkan_AddTexture()) on Vulkan, an SDL_Texture* on SDL
        virtual void* GetImGuiTextureId() const = 0;
    };

}  // namespace BECore
