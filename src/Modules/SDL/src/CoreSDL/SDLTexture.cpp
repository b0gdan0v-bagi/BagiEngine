#include "SDLTexture.h"

#include <Generated/SDLTexture.gen.hpp>
#include <SDL3/SDL.h>

namespace BECore {

    SDLTexture::~SDLTexture() {
        if (_texture) {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
    }

    ResourceState SDLTexture::GetState() const {
        return _state;
    }

    PoolString SDLTexture::GetPath() const {
        return _path;
    }

    uint64_t SDLTexture::GetMemoryUsage() const {
        return static_cast<uint64_t>(_width * _height * 4);
    }

    PoolString SDLTexture::GetTypeName() const {
        return "SDLTexture"_intern;
    }

    float SDLTexture::GetWidth() const {
        return _width;
    }

    float SDLTexture::GetHeight() const {
        return _height;
    }

    void SDLTexture::SetLoaded(PoolString path, SDL_Texture* texture, float width, float height) {
        _path = path;
        _texture = texture;
        _width = width;
        _height = height;
        _state = ResourceState::Loaded;
    }

    void SDLTexture::SetFailed(PoolString path) {
        _path = path;
        _state = ResourceState::Failed;
    }

}  // namespace BECore
