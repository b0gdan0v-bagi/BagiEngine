#pragma once

#include <BECore/Renderer/BlendMode.h>
#include <BECore/Renderer/DrawTransform.h>
#include <BECore/Renderer/GeometryDetail.h>
#include <BECore/Renderer/RenderCounters.h>
#include <BECore/Renderer/ViewTransform.h>

namespace BECore {

    class IDeserializer;
    class IGpuDevice;
    class IMainWindow;
    class ITexture;
    class IRenderTarget;

    /// An image of the output: RGBA8 (red first), the rows packed one after another from the top, so `rgba.size()` is `width * height * 4`
    struct PixelBuffer {
        uint32_t width = 0;
        uint32_t height = 0;
        eastl::vector<uint8_t> rgba;
    };

    /// One corner of a triangle for IRenderer::DrawGeometry()
    struct GeometryVertex {
        float x = 0.0f;  ///< the position, in the units of DrawFilledRect() (the translation and the view transform are applied to it)
        float y = 0.0f;
        Color color;     ///< straight alpha; it multiplies the texel of a textured draw, or is the color itself of an untextured one
        float u = 0.0f;  ///< the position in the texture, 0 to 1 over the whole texture
        float v = 0.0f;
    };

    /// The image, or why it could not be taken
    using CaptureResult = std::expected<PixelBuffer, eastl::string>;
    using CaptureCallback = std::function<void(CaptureResult)>;

    class IRenderer : public RefCounted, public SubscriptionHolder {
        BE_CLASS(IRenderer, FACTORY_BASE)
    public:
        IRenderer();
        ~IRenderer() override = default;

        // Tells one renderer from another for as long as the process runs, even when a later one is allocated at the
        // address of an earlier one. Whoever keeps GPU objects of a renderer (the glyph atlases) remembers it.
        [[nodiscard]] uint64_t GetId() const {
            return _id;
        }

        // Blocks until the GPU has finished everything that was submitted. Call it before releasing textures that a
        // frame in flight may still use. The default does nothing (the SDL renderer has no frames in flight).
        virtual void WaitIdle() {}

        // The GPU interface of a renderer that can draw in 3D (Renderer/Gpu/IGpuDevice.h), or null: the SDL 2D renderer has none. Owned by the
        // renderer, valid until Destroy(). A 3D caller goes through RequireGpuDevice(), which says why when there is none.
        // Not BE_FUNCTION (a raw pointer of a type the control API does not know).
        [[nodiscard]] virtual IGpuDevice* GetGpuDevice() {
            return nullptr;
        }

        // Why GetGpuDevice() is null, for the log line of a caller that wanted 3D (the default is generic, a backend says what it lacks)
        [[nodiscard]] virtual eastl::string GetNoGpuDeviceReason() const;

        // How much detail generated geometry (the shadows and glows of UIBoxMesh) should have for this renderer: Full by default, Reduced for the SDL
        // software renderer, where every blended triangle costs a surface and a blit. Not BE_FUNCTION (an engine query, not a control API call).
        [[nodiscard]] virtual GeometryDetail GetGeometryDetail() const {
            return GeometryDetail::Full;
        }

        BE_FUNCTION virtual bool Initialize(IMainWindow& window) = 0;
        BE_FUNCTION virtual void Destroy() = 0;
        BE_FUNCTION virtual void BeginFrame() = 0;
        BE_FUNCTION virtual void EndFrame() = 0;
        BE_FUNCTION virtual void Clear(const Color& color) = 0;
        BE_FUNCTION virtual void Present() = 0;
        BE_FUNCTION virtual void DrawFilledRect(float x, float y, float w, float h, const Color& color) = 0;
        // The texture multiplied by @p tint: white leaves it as it is, the alpha of the tint fades it, a color tints it.
        // Not BE_FUNCTION: the generated table takes `&IRenderer::DrawTexture`, which an overloaded name makes ambiguous.
        virtual void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint) = 0;

        // Draws the texture untinted. A backend that overrides the tinted overload adds `using IRenderer::DrawTexture;`.
        void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH) {
            DrawTexture(texture, srcRect, dstX, dstY, dstW, dstH, kWhite);
        }

        // Draws indexed triangles (three indices per triangle) with a color and a texture coordinate per vertex, the primitive of a library that
        // brings its own layout and draws shapes of any form (RmlUi). @p texture is optional: without one the vertex colors are the fill. Each position
        // is moved by (@p translateX, @p translateY), then by the view transform, and the draw obeys the clip like the other primitives. Alpha is
        // straight and blended over the target. An index past the vertices ends the draw.
        // Not BE_FUNCTION (spans). The default logs once and draws nothing, for a backend without the primitive (the SDL and the Vulkan backends both have it,
        // .claude/rules/rendering-module.md).
        virtual void DrawGeometry(ITexture* texture, eastl::span<const GeometryVertex> vertices, eastl::span<const int> indices, float translateX, float translateY);

        // Draws a rect rotated about @p transform's pivot (absolute, in the units of the call). An identity transform is DrawFilledRect() unchanged;
        // otherwise the default builds the four rotated corners (MakeTransformedQuad()) and draws them through DrawGeometry(), which applies the view
        // transform, the clip and the blend mode, so it works on every backend that has DrawGeometry() and no backend needs to override it.
        // Not BE_FUNCTION, like DrawTexture() and DrawGeometry().
        virtual void DrawTransformedRect(float x, float y, float w, float h, const Color& color, const DrawTransform& transform);

        // Draws a texture like DrawTexture() but rotated about @p transform's pivot and mirrored by its flips (the UVs swap). An identity transform is
        // DrawTexture() unchanged; otherwise the default draws a textured quad through DrawGeometry() with the UVs of @p srcRect over the texture size
        // (the whole texture when @p srcRect is null or empty) and @p tint as the vertex color. A texture without size draws nothing. A backend can
        // override it with a native draw (SDLRendererBackend does, with SDL_RenderTextureRotated).
        virtual void DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform);

        // Makes a texture from pixels in memory: RGBA8 (red first), straight alpha, rows packed from the top, so
        // @p rgba holds `width * height * 4` bytes. The pixels are copied. Empty when the size is zero or the backend fails.
        // Main thread only, like every GPU call (a loader running on a pool thread must not call it).
        [[nodiscard]] virtual IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) = 0;

        // Render-to-texture: create an offscreen target, render into it, then display via ImGui::Image.
        // Call SetRenderTarget() before drawing scene content, UnsetRenderTarget() after.
        // Must be called when no other render pass is active (before ClearScreenWidget opens the main pass).
        BE_FUNCTION virtual IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) = 0;
        BE_FUNCTION virtual void SetRenderTarget(IRenderTarget* target) = 0;
        BE_FUNCTION virtual void UnsetRenderTarget() = 0;

        // Size in pixels of the target being drawn into: the offscreen target between SetRenderTarget() and
        // UnsetRenderTarget(), else the back buffer while its pass is open. 0 before the first pass.
        virtual float GetOutputWidth() const = 0;
        virtual float GetOutputHeight() const = 0;

        // Asks for the image of the back buffer: @p callback gets it once, on the calling thread, just before the frame that
        // is being drawn is presented (so it is called from Present() or the RenderPresentEvent, not from here). A new request
        // replaces one that has not been served, whose callback then gets an error. A backend that cannot read its output
        // back keeps this default, which calls @p callback at once with an error (the Vulkan backend: .claude/rules/rendering-module.md).
        virtual void RequestCapture(CaptureCallback callback);

        // How the draws combine with the target (Alpha by default). State of the interface, like the view transform: SDL applies it to every
        // primitive (DrawFilledRect, DrawTexture, DrawGeometry and so the transformed draws); Vulkan draws straight alpha whatever it says and warns
        // once (BlendMode.h). Whoever sets another mode puts the previous one back
        // (ScopedBlendMode), so a draw further on never sees a mode that leaked.
        void SetBlendMode(BlendMode mode) {
            _blendMode = mode;
        }
        [[nodiscard]] BlendMode GetBlendMode() const {
            return _blendMode;
        }

        // The opacity of what is drawn next, 0 to 1 (1 by default; a NaN is 1, a value outside the range is cut to it). State of the interface like the
        // blend mode: every primitive (DrawFilledRect, DrawTexture, DrawGeometry, so text, UI boxes, particles and the transformed draws) multiplies its
        // alpha by it, once, in both backends. It is a per-draw multiply, not a composite of a group, so overlapping draws blend through each other.
        // Whoever sets another value puts the previous one back (ScopedOpacity, which nests by multiplying).
        void SetOpacity(float opacity) {
            _opacity = ClampOpacity(opacity);
        }
        [[nodiscard]] float GetOpacity() const {
            return _opacity;
        }
        /// An opacity in the range the renderer keeps: 0 to 1, a NaN is 1
        [[nodiscard]] static constexpr float ClampOpacity(float opacity) {
            if (!(opacity == opacity)) {
                return 1.0f;
            }
            return opacity < 0.0f ? 0.0f : (opacity > 1.0f ? 1.0f : opacity);
        }

        // Maps the coordinates of DrawFilledRect() and DrawTexture() to output pixels (the identity by default). The
        // scene sets it around its draw pass to fit its view to the output (Scene::Draw()). The backends apply it.
        void SetViewTransform(const ViewTransform& transform) {
            _viewTransform = transform;
        }
        const ViewTransform& GetViewTransform() const {
            return _viewTransform;
        }

        // Clips DrawFilledRect() and DrawTexture() to a rect in output pixels (after the view transform, so a
        // scene transform does not move it). Rects nest: the clip is the intersection of every pushed rect, and the
        // pop restores the one before. An unbalanced push leaks into the next frame, so pair them (RAII at the call site).
        void PushClipRect(const Rect& rect);
        void PopClipRect();

        // The current clip in output pixels, or nothing when no rect is pushed. A pushed rect that does not overlap
        // the one before it leaves an empty clip, which hides everything (see IsClipEmpty()).
        [[nodiscard]] std::optional<Rect> GetClipRect() const;
        [[nodiscard]] bool IsClipEmpty() const;
        [[nodiscard]] size_t GetClipDepth() const {
            return _clipStack.size();
        }

        // The draws counted since the last call, and a fresh start (the frame statistics take them once per frame, FrameStatsCollector).
        // Counted by the backends where a primitive really reaches the GPU, after their early returns, so a draw that a clip or a missing
        // texture drops is not counted, and a composed draw (DrawTransformedRect over DrawGeometry) is counted once.
        RenderCounters TakeRenderCounters();
        [[nodiscard]] const RenderCounters& PeekRenderCounters() const {
            return _counters;
        }
        // While any pause is alive the draws are not counted (the profiler's own overlay, ScopedRenderCountersPause)
        void PauseCounters() {
            ++_countersPaused;
        }
        void ResumeCounters() {
            --_countersPaused;
        }

    protected:
        // Called after the clip changes (push or pop). A backend applies GetClipRect() to its next draws.
        virtual void OnClipRectChanged() {}

        // A backend calls it once per primitive it submits: @p texture is null for an untextured draw
        void CountDraw(const ITexture* texture, uint64_t triangles) {
            if (_countersPaused > 0) {
                return;
            }
            ++_counters.drawCalls;
            _counters.triangles += triangles;
            if (texture != nullptr && texture != _lastCountedTexture) {
                ++_counters.textureSwitches;
                _lastCountedTexture = texture;
            }
        }
        static constexpr uint64_t kQuadTriangles = 2;

        /// The one place the opacity rule is written: @p color with its alpha times GetOpacity(), rounded. A backend calls it once per primitive;
        /// at opacity 1 the color comes back untouched.
        [[nodiscard]] Color ApplyOpacity(const Color& color) const {
            if (_opacity >= 1.0f) {
                return color;
            }
            Color result = color;
            result.a = static_cast<unsigned char>(static_cast<float>(color.a) * _opacity + kOpacityRounding);
            return result;
        }

    private:
        static constexpr float kOpacityRounding = 0.5f;
        static constexpr Color kWhite{255, 255, 255, 255};

        uint64_t _id = 0;
        bool _geometryWarned = false;
        ViewTransform _viewTransform;
        BlendMode _blendMode = BlendMode::Alpha;
        float _opacity = 1.0f;
        // Each entry is already intersected with the one below it
        eastl::vector<Rect> _clipStack;
        RenderCounters _counters;
        const ITexture* _lastCountedTexture = nullptr;  // compared only, never dereferenced
        int _countersPaused = 0;
    };

    /// The GPU device of @p renderer, or the renderer's name and why it has none. What a 3D caller uses, so the game logs the error once (LOG_ERROR at
    /// the caller) instead of drawing a blank screen.
    [[nodiscard]] std::expected<IGpuDevice*, eastl::string> RequireGpuDevice(IRenderer& renderer);

    /// Stops a renderer counting its draws for the length of a scope
    class ScopedRenderCountersPause {
    public:
        explicit ScopedRenderCountersPause(IRenderer& renderer) : _renderer(renderer) {
            _renderer.PauseCounters();
        }
        ~ScopedRenderCountersPause() {
            _renderer.ResumeCounters();
        }

        ScopedRenderCountersPause(const ScopedRenderCountersPause&) = delete;
        ScopedRenderCountersPause& operator=(const ScopedRenderCountersPause&) = delete;

    private:
        IRenderer& _renderer;
    };

    /// Sets a blend mode on a renderer for the length of a scope and puts the previous one back
    class ScopedBlendMode {
    public:
        ScopedBlendMode(IRenderer& renderer, BlendMode mode) : _renderer(renderer), _previous(renderer.GetBlendMode()) {
            _renderer.SetBlendMode(mode);
        }
        ~ScopedBlendMode() {
            _renderer.SetBlendMode(_previous);
        }

        ScopedBlendMode(const ScopedBlendMode&) = delete;
        ScopedBlendMode& operator=(const ScopedBlendMode&) = delete;

    private:
        IRenderer& _renderer;
        BlendMode _previous;
    };

    /// Multiplies the opacity of a renderer by @p factor for the length of a scope and puts the previous value back, so a subtree inside a subtree
    /// draws with the product. A NaN factor is 1.
    class ScopedOpacity {
    public:
        ScopedOpacity(IRenderer& renderer, float factor) : _renderer(renderer), _previous(renderer.GetOpacity()) {
            _renderer.SetOpacity(_previous * IRenderer::ClampOpacity(factor));
        }
        ~ScopedOpacity() {
            _renderer.SetOpacity(_previous);
        }

        ScopedOpacity(const ScopedOpacity&) = delete;
        ScopedOpacity& operator=(const ScopedOpacity&) = delete;

    private:
        IRenderer& _renderer;
        float _previous;
    };

}  // namespace BECore
