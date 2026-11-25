#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>

struct SDL_Renderer;

namespace BECore {

    namespace RenderEvents {
        struct SetRenderDrawColorEvent;
    }

    class SDLRenderTarget;

    class SDLRendererBackend : public IRenderer {
        BE_CLASS(SDLRendererBackend)
    public:
        SDLRendererBackend() = default;
        ~SDLRendererBackend() override;

        bool Initialize(IMainWindow& window) override;
        void Destroy() override;
        void BeginFrame() override;
        void EndFrame() override;
        /// Fills the current target at once and remembers @p color for the next SetRenderTarget(), which clears its target with it and goes back to opaque black (as Vulkan does)
        void Clear(const Color& color) override;
        void Present() override;
        void DrawFilledRect(float x, float y, float w, float h, const Color& color) override;
        using IRenderer::DrawTexture;
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) override;
        /// A transformed texture is drawn with SDL_RenderTextureRotated; the rect keeps the geometry default of IRenderer
        void DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform) override;
        IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) override;
        void DrawGeometry(ITexture* texture, Span<const GeometryVertex> vertices, Span<const int> indices, float translateX, float translateY) override;

        /// A RGBA target texture; empty for a zero side or when SDL refuses it. It may outlive the renderer, which releases its texture in Destroy()
        IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) override;
        /// Redirects every draw into @p target and clears it with the colour of the last Clear(). A target of another backend, or one whose renderer is gone, keeps the back buffer (logged once)
        void SetRenderTarget(IRenderTarget* target) override;
        void UnsetRenderTarget() override;

        /// The SDL 2D renderer has no GPU device (GetGpuDevice() stays null): no depth buffer and no shaders, so no 3D
        String GetNoGpuDeviceReason() const override;

        float GetOutputWidth() const override;
        float GetOutputHeight() const override;

        /// Reduced when SDL made its software renderer (the headless game, and a window where no GPU driver came up), Full for a GPU driver
        [[nodiscard]] GeometryDetail GetGeometryDetail() const override;

        /// Serves the request just before the next SDL_RenderPresent with SDL_RenderReadPixels
        void RequestCapture(CaptureCallback callback) override;

        SDL_Renderer* GetSDLRenderer() const {
            return _renderer;
        }

    protected:
        /// Hands the clip rect to SDL; the draw calls skip everything while the clip is empty
        void OnClipRectChanged() override;

    private:
        friend class SDLRenderTarget;

        void OnTargetDestroyed(SDLRenderTarget& target);
        void ReleaseTargets();
        void OnRenderClear();
        void OnRenderPresent();
        void PresentFrame();
        void ServeCapture();
        void OnSetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event);

        bool _software = false;  // SDL's own software renderer: every blended triangle costs a surface and a blit

        // Scratch of DrawGeometry(): the vertices in the planar layout SDL_RenderGeometryRaw takes, kept between calls so a draw does not allocate
        Vector<float> _geometryPositions;
        Vector<float> _geometryColors;
        Vector<float> _geometryTexCoords;

        void OnImGuiInit();
        void OnImGuiShutdown();
        void OnImGuiNewFrame();
        void OnImGuiRender();

        SDL_Renderer* _renderer = nullptr;
        CaptureCallback _captureCallback;
        Vector<SDLRenderTarget*> _targets;  // the live targets, not owned
        SDLRenderTarget* _currentTarget = nullptr;
        Color _nextTargetClear{0, 0, 0, 255};
        bool _foreignTargetWarned = false;
    };

}  // namespace BECore
