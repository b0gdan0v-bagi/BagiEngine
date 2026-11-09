#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>

struct SDL_Renderer;

namespace BECore {

    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }

    class SDLRendererBackend : public IRenderer {
        BE_CLASS(SDLRendererBackend)
    public:
        SDLRendererBackend() = default;
        ~SDLRendererBackend() override = default;

        bool Initialize(IMainWindow& window) override;
        void Destroy() override;
        void BeginFrame() override;
        void EndFrame() override;
        void Clear(const Color& color) override;
        void Present() override;
        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override;
        using IRenderer::DrawTexture;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) override;
        /// A transformed texture is drawn with SDL_RenderTextureRotated; the rect keeps the geometry default of IRenderer
        void DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform) override;
        IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override;
        void DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY) override;

        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) override;
        void SetRenderTarget(IRenderTarget* target) override;
        void UnsetRenderTarget() override;

        float GetOutputWidth() const override;
        float GetOutputHeight() const override;

        /// Serves the request just before the next SDL_RenderPresent with SDL_RenderReadPixels
        void RequestCapture(CaptureCallback callback) override;

        SDL_Renderer* GetSDLRenderer() const {
            return _renderer;
        }

    protected:
        /// Hands the clip rect to SDL; the draw calls skip everything while the clip is empty
        void OnClipRectChanged() override;

    private:
        void OnRenderClear();
        void OnRenderPresent();
        void PresentFrame();
        void ServeCapture();
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        // Scratch of DrawGeometry(): the vertices in the planar layout SDL_RenderGeometryRaw takes, kept between calls so a draw does not allocate
        eastl::vector<float> _geometryPositions;
        eastl::vector<float> _geometryColors;
        eastl::vector<float> _geometryTexCoords;

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        SDL_Renderer* _renderer = nullptr;
        CaptureCallback _captureCallback;
    };

}  // namespace BECore
