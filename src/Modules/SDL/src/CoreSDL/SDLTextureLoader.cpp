#include "SDLTextureLoader.h"

#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <CoreSDL/ImageFile.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLTexture.h>
#include <Generated/SDLTextureLoader.gen.hpp>
#include <SDL3/SDL.h>

namespace BECore {

    bool SDLTextureLoader::CanLoad(StringView extension) const {
        if (extension != ".bmp" && extension != ".png" && extension != ".jpg" && extension != ".jpeg") {
            return false;
        }
        // Only handle textures when the active renderer is the SDL backend.
        const auto& renderer = CoreManager::GetRenderer();
        return renderer && dynamic_cast<SDLRendererBackend*>(renderer.Get()) != nullptr;
    }

    Task<IntrusivePtr<IResource>> SDLTextureLoader::LoadAsync(PoolString path) {
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> SDLTextureLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<SDLTexture> SDLTextureLoader::LoadInternal(PoolString path) {
        BE_PROFILE_SCOPE("SDLTextureLoader::Load");
        auto texture = New<SDLTexture>();

        auto* sdlBackend = dynamic_cast<SDLRendererBackend*>(CoreManager::GetRenderer().Get());
        if (!sdlBackend || !sdlBackend->GetSDLRenderer()) {
            LOG_ERROR("SDLTextureLoader: renderer not available when loading '{}'", path.ToStringView());
            texture->SetFailed(path);
            return texture;
        }

        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        auto image = ReadImageFile(realPath);
        if (!image) {
            LOG_ERROR("SDLTextureLoader: failed to load image '{}': {}", path.ToStringView(), image.error());
            texture->SetFailed(path);
            return texture;
        }

        const int width = static_cast<int>(image->width);
        SDL_Surface* surface = SDL_CreateSurfaceFrom(width, static_cast<int>(image->height), SDL_PIXELFORMAT_RGBA32, image->pixels.data(), width * static_cast<int>(kBytesPerPixel));
        if (!surface) {
            LOG_ERROR("SDLTextureLoader: failed to wrap the pixels of '{}': {}", path.ToStringView(), SDL_GetError());
            texture->SetFailed(path);
            return texture;
        }

        SDL_Texture* sdlTexture = SDL_CreateTextureFromSurface(sdlBackend->GetSDLRenderer(), surface);
        SDL_DestroySurface(surface);

        if (!sdlTexture) {
            LOG_ERROR("SDLTextureLoader: failed to create texture from '{}': {}", path.ToStringView(), SDL_GetError());
            texture->SetFailed(path);
            return texture;
        }

        sdlBackend->CountTextureUpload(image->width, image->height);

        float texWidth = 0.0f;
        float texHeight = 0.0f;
        SDL_GetTextureSize(sdlTexture, &texWidth, &texHeight);

        texture->SetLoaded(path, sdlTexture, texWidth, texHeight);
        LOG_INFO("SDLTextureLoader: loaded texture '{}' ({}x{})", path.ToStringView(), texWidth, texHeight);

        return texture;
    }

}  // namespace BECore
