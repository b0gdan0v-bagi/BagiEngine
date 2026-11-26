#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>

struct SDL_Renderer;
struct SDL_Texture;
struct ImDrawData;

namespace BECore {

    class SDLRenderTarget;

    class SDLRendererBackend : public IRenderer {
        BE_CLASS(SDLRendererBackend)
    public:
        SDLRendererBackend() = default;
        ~SDLRendererBackend() override;

        bool Initialize(IMainWindow& window) override;
        void SetUncapped(bool uncapped) override {
            _uncapped = uncapped;
        }
        void Destroy() override;
        void BeginFrame(const Color& clearColor) override;
        void EndFrame() override;
        /// Outside a frame fills the current target at once; inside one (BeginFrame() to the present) only names the colour. Remembers @p color for the next SetRenderTarget(), which clears its target
        /// with it and goes back to opaque black (as Vulkan does)
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

        /// Draws ImGui's frame over what is drawn so far, at the frame's framebuffer scale. Needs ImGui's SDL renderer backend set up on this renderer
        void DrawImGui(ImDrawData& data);

        /// On SDL's software renderer only the draws that a capture test proved byte-equal to their immediate form are batched (true, the default):
        /// a rect on whole output pixels that is opaque or additive or multiplied, and a transformed rect, which is geometry either way; textured quads
        /// are not (the triangle path shifts their texels, `.claude/rules/rendering-module.md`, "Software rasterizer"). False batches every draw, so a
        /// test can compare the two images. A GPU driver batches everything whatever this says.
        void SetBatchOnlyExactDraws(bool exactOnly) {
            _batchOnlyExact = exactOnly;
        }
        [[nodiscard]] bool IsBatchOnlyExactDraws() const {
            return _batchOnlyExact;
        }

    protected:
        /// Hands the clip rect to SDL; the draw calls skip everything while the clip is empty
        void OnClipRectChanged() override;

        /// One SDL_RenderGeometryRaw for the whole batch (the vertices are already in output pixels)
        void SubmitBatch(const RenderBatch& batch) override;

        /// Only an SDLTexture with a live handle
        [[nodiscard]] bool CanDrawTexture(const ITexture& texture) const override;

    private:
        friend class SDLRenderTarget;

        void OnTargetDestroyed(SDLRenderTarget& target);
        void ReleaseTargets();
        void OnRenderPresent();
        void PresentFrame();
        void ServeCapture();

        /// Fills the scratch arrays from @p vertices (position moved by the translation and mapped by @p view, the color faded by the opacity
        /// when @p fade) and queues one SDL_RenderGeometryRaw; false when SDL refused it. The indices are checked by the caller.
        bool QueueGeometry(SDL_Texture* handle, Span<const GeometryVertex> vertices, Span<const int> indices, float translateX, float translateY, const ViewTransform& view, bool fade,
                           BlendMode blend);

        bool _uncapped = false;  // SetUncapped(): vsync stays off
        bool _software = false;  // SDL's own software renderer: every blended triangle costs a surface and a blit
        bool _batchOnlyExact = true;

        /// Whether a draw that the renderer batches only where it is exact may be appended: always on a GPU driver
        [[nodiscard]] bool BatchesEverything() const {
            return !_software || !_batchOnlyExact;
        }
        /// Whether DrawFilledRect() of this rect and color may be appended under the gate above
        [[nodiscard]] bool BatchesRect(float x, float y, float w, float h, const Color& color) const;

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
        bool _frameOpen = false;  // between BeginFrame() and the present: Clear() then only names the colour of the next target
        bool _foreignTargetWarned = false;
    };

}  // namespace BECore
