#pragma once

#include <BECore/Renderer/ITexture.h>

struct SDL_Texture;

namespace BECore {

    class SDLTextureLoader;

    class SDLTexture : public ITexture {
        BE_CLASS(SDLTexture)
    public:
        SDLTexture() = default;
        ~SDLTexture() override;

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        float GetWidth() const override;
        float GetHeight() const override;

        void* GetNativeHandle() const override {
            return static_cast<void*>(_texture);
        }

        SDL_Texture* GetSDLTexture() const {
            return _texture;
        }

    private:
        friend class SDLTextureLoader;

        void SetLoaded(PoolString path, SDL_Texture* texture, float width, float height);
        void SetFailed(PoolString path);

        SDL_Texture* _texture = nullptr;
        PoolString _path;
        ResourceState _state = ResourceState::Unloaded;
        float _width = 0.0f;
        float _height = 0.0f;
    };

}  // namespace BECore
