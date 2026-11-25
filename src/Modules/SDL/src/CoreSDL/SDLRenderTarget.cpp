#include "SDLRenderTarget.h"

#include <CoreSDL/SDLRendererBackend.h>
#include <SDL3/SDL.h>

namespace BECore {

    SDLRenderTarget::SDLRenderTarget(SDLRendererBackend& owner, SDL_Texture* texture, uint32_t width, uint32_t height) : _owner(&owner), _texture(texture), _width(width), _height(height) {}

    SDLRenderTarget::~SDLRenderTarget() {
        if (_owner) {
            _owner->OnTargetDestroyed(*this);  // unsets it when it is the current one, before its texture goes
        }
        if (_texture) {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
    }

    void* SDLRenderTarget::GetImGuiTextureId() const {
        return static_cast<void*>(_texture);
    }

    void SDLRenderTarget::OnRendererDestroyed() {
        if (_texture) {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
        _owner = nullptr;
    }

}  // namespace BECore
