#include "VulkanTextureLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <CoreVulkan/VulkanRendererBackend.h>
#include <CoreVulkan/VulkanTexture.h>
#include <CoreVulkan/VulkanTextureUpload.h>
#include <Generated/VulkanTextureLoader.gen.hpp>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    bool VulkanTextureLoader::CanLoad(StringView extension) const {
        if (extension != ".bmp" && extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            return false;
        }
        // Only handle textures when the active renderer is the Vulkan backend.
        const auto& renderer = CoreManager::GetRenderer();
        return renderer && dynamic_cast<VulkanRendererBackend*>(renderer.Get()) != nullptr;
    }

    Task<IntrusivePtr<IResource>> VulkanTextureLoader::LoadAsync(PoolString path) {
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> VulkanTextureLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<VulkanTexture> VulkanTextureLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("VulkanTextureLoader::Load");
        auto* backend = dynamic_cast<VulkanRendererBackend*>(CoreManager::GetRenderer().Get());
        if (!backend) {
            LOG_ERROR(Format("VulkanTextureLoader: renderer is not VulkanRendererBackend when loading '{}'", path.ToStringView()).c_str());
            auto texture = New<VulkanTexture>();
            texture->SetFailed(path);
            return texture;
        }

        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        SDL_Surface* rawSurface = IMG_Load(realPath.string().c_str());
        if (!rawSurface) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to load image '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            auto texture = New<VulkanTexture>();
            texture->SetFailed(path);
            return texture;
        }

        // Bytes in memory must be R,G,B,A to match VK_FORMAT_R8G8B8A8_SRGB. RGBA32 is that byte order on any
        // endianness; the packed RGBA8888 is A,B,G,R in memory on little-endian and would swap the channels.
        SDL_Surface* surface = SDL_ConvertSurface(rawSurface, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(rawSurface);
        if (!surface) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to convert surface '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            auto texture = New<VulkanTexture>();
            texture->SetFailed(path);
            return texture;
        }

        auto texture = VulkanTextureUpload::Upload(*backend, path, static_cast<const uint8_t*>(surface->pixels), static_cast<uint32_t>(surface->w), static_cast<uint32_t>(surface->h));
        SDL_DestroySurface(surface);
        return texture;
    }

}  // namespace BECore
