#pragma once

#include <BECore/Renderer/IRenderTarget.h>

struct SDL_Texture;

namespace BECore {

    class SDLRendererBackend;

    /// An offscreen target of the SDL renderer: a texture made with SDL_TEXTUREACCESS_TARGET. The renderer keeps a list of the
    /// live ones and takes their texture away in Destroy(), so a target that outlives the renderer holds no dead handle.
    class SDLRenderTarget : public IRenderTarget {
    public:
        SDLRenderTarget(SDLRendererBackend& owner, SDL_Texture* texture, uint32_t width, uint32_t height);
        ~SDLRenderTarget() override;

        uint32_t GetWidth() const override {
            return _width;
        }
        uint32_t GetHeight() const override {
            return _height;
        }
        void* GetImGuiTextureId() const override;

        /// Null once the renderer has been destroyed
        SDL_Texture* GetSDLTexture() const {
            return _texture;
        }

    private:
        friend class SDLRendererBackend;

        /// Called by the renderer before it destroys itself: the texture goes with it
        void OnRendererDestroyed();

        SDLRendererBackend* _owner = nullptr;
        SDL_Texture* _texture = nullptr;
        uint32_t _width = 0;
        uint32_t _height = 0;
    };

}  // namespace BECore
