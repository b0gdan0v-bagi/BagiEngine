#include "SDLTextureLoader.h"

#include <BECore/GameManager/CoreManager.h>
#include <CoreSDL/SDLRendererBackend.h>
#include <CoreSDL/SDLTexture.h>
#include <Generated/SDLTextureLoader.gen.hpp>
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

namespace BECore {

    bool SDLTextureLoader::CanLoad(eastl::string_view extension) const {
        return extension == ".bmp" || extension == ".png" || extension == ".jpg" || extension == ".jpeg";
    }

    Task<IntrusivePtr<IResource>> SDLTextureLoader::LoadAsync(PoolString path) {
        co_return LoadInternal(path);
    }

    IntrusivePtr<IResource> SDLTextureLoader::LoadSync(PoolString path) {
        return LoadInternal(path);
    }

    IntrusivePtr<SDLTexture> SDLTextureLoader::LoadInternal(PoolString path) {
        auto texture = New<SDLTexture>();

        auto* sdlBackend = dynamic_cast<SDLRendererBackend*>(CoreManager::GetRenderer().Get());
        if (!sdlBackend || !sdlBackend->GetSDLRenderer()) {
            LOG_ERROR(Format("SDLTextureLoader: renderer not available when loading '{}'", path.ToStringView()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        const auto realPath = CoreManager::GetFileSystem().ResolvePath(path.ToStringView());
        SDL_Surface* surface = IMG_Load(realPath.string().c_str());
        if (!surface) {
            LOG_ERROR(Format("SDLTextureLoader: failed to load image '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        SDL_Texture* sdlTexture = SDL_CreateTextureFromSurface(sdlBackend->GetSDLRenderer(), surface);
        SDL_DestroySurface(surface);

        if (!sdlTexture) {
            LOG_ERROR(Format("SDLTextureLoader: failed to create texture from '{}': {}", path.ToStringView(), SDL_GetError()).c_str());
            texture->SetFailed(path);
            return texture;
        }

        float texWidth = 0.0f;
        float texHeight = 0.0f;
        SDL_GetTextureSize(sdlTexture, &texWidth, &texHeight);

        texture->SetLoaded(path, sdlTexture, texWidth, texHeight);
        LOG_INFO(Format("SDLTextureLoader: loaded texture '{}' ({}x{})", path.ToStringView(), texWidth, texHeight).c_str());

        return texture;
    }

}  // namespace BECore
