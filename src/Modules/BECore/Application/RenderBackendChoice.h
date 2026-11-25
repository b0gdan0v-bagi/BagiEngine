#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief The renderer backend one run asks for (`--backend=`); Auto is "not given": the config's `renderer`, the software one when headless.
     *
     * SdlGpu is SDLRendererBackend on SDL's hardware renderer (Metal on macOS), Vulkan is VulkanRendererBackend, Software is the pixel-exact headless one.
     */
    CORE_ENUM(RenderBackendChoice, uint8_t, Auto, Software, SdlGpu, Vulkan)

    namespace RenderBackendSpelling {
        inline constexpr StringView kSoftware = "software";
        inline constexpr StringView kSdlGpu = "sdl-gpu";
        inline constexpr StringView kVulkan = "vulkan";
    }  // namespace RenderBackendSpelling

    /// The choice spelled by @p text (`software`, `sdl-gpu`, `vulkan`); nullopt for any other text, `Auto` included (it is "not given", never spelled)
    [[nodiscard]] inline Optional<RenderBackendChoice> ParseRenderBackend(StringView text) {
        if (text == RenderBackendSpelling::kSoftware) {
            return RenderBackendChoice::Software;
        }
        if (text == RenderBackendSpelling::kSdlGpu) {
            return RenderBackendChoice::SdlGpu;
        }
        if (text == RenderBackendSpelling::kVulkan) {
            return RenderBackendChoice::Vulkan;
        }
        return std::nullopt;
    }

    /// The spelling of @p choice as the command line and the "not available" message write it; empty for Auto
    [[nodiscard]] inline StringView RenderBackendName(RenderBackendChoice choice) {
        switch (choice) {
            case RenderBackendChoice::Software:
                return RenderBackendSpelling::kSoftware;
            case RenderBackendChoice::SdlGpu:
                return RenderBackendSpelling::kSdlGpu;
            case RenderBackendChoice::Vulkan:
                return RenderBackendSpelling::kVulkan;
            case RenderBackendChoice::Auto:
                break;
        }
        return {};
    }

}  // namespace BECore
