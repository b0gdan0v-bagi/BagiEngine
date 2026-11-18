#include "VulkanTextureLoader.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/ImageFile.h>
#include <CoreVulkan/VulkanRendererBackend.h>
#include <CoreVulkan/VulkanTexture.h>
#include <CoreVulkan/VulkanTextureUpload.h>
#include <Generated/VulkanTextureLoader.gen.hpp>

namespace BECore {

    bool VulkanTextureLoader::CanLoad(StringView extension) const {
        if (extension != ".bmp" && extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            return false;
        }
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
        // The bytes are R,G,B,A in memory, which is what VK_FORMAT_R8G8B8A8_SRGB reads
        const auto image = ReadImageFile(realPath);
        if (!image) {
            LOG_ERROR(Format("VulkanTextureLoader: failed to load image '{}': {}", path.ToStringView(), image.error()).c_str());
            auto texture = New<VulkanTexture>();
            texture->SetFailed(path);
            return texture;
        }

        return VulkanTextureUpload::Upload(*backend, path, image->pixels.data(), image->width, image->height);
    }

}  // namespace BECore
