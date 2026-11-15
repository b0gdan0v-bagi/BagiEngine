#pragma once

#include <BECore/Math/Color.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Renderer/BlendMode.h>
#include <BECore/Renderer/DrawTransform.h>
#include <BECore/Renderer/GeometryDetail.h>
#include <BECore/Renderer/RenderCounters.h>
#include <BECore/Renderer/ViewTransform.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>
#include <cstdint>
#include <expected>
#include <functional>

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
        Vector<uint8_t> rgba;
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
    using CaptureResult = std::expected<PixelBuffer, String>;
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

        // The GPU interface (Renderer/Gpu/IGpuDevice.h), or null: the SDL 2D renderer has none. Owned by the renderer, valid until Destroy().
        // A 3D caller goes through RequireGpuDevice(). Not BE_FUNCTION (a raw pointer the control API does not know).
        [[nodiscard]] virtual IGpuDevice* GetGpuDevice() {
            return nullptr;
        }

        // Why GetGpuDevice() is null, for the log line of a caller that wanted 3D
        [[nodiscard]] virtual String GetNoGpuDeviceReason() const;

        // How much detail generated geometry (the shadows and glows of UIBoxMesh) should have: Reduced where each blended triangle costs a surface
        // and a blit (the SDL software renderer). Not BE_FUNCTION (an engine query, not a control API call).
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

        // Draws indexed triangles (three indices per triangle), the primitive of a library that brings its own layout (RmlUi). @p texture is
        // optional: without one the vertex colors are the fill. Each position is moved by (@p translateX, @p translateY), then by the view
        // transform; the clip applies. An index past the vertices ends the draw.
        // Not BE_FUNCTION (spans). The default logs once and draws nothing, for a backend without the primitive.
        virtual void DrawGeometry(ITexture* texture, Span<const GeometryVertex> vertices, Span<const int> indices, float translateX, float translateY);

        // Draws a rect rotated about @p transform's pivot (absolute, in the units of the call). An identity transform is DrawFilledRect() unchanged;
        // otherwise the default draws the rotated quad (MakeTransformedQuad()) through DrawGeometry(), so no backend needs to override it.
        // Not BE_FUNCTION, like DrawTexture() and DrawGeometry().
        virtual void DrawTransformedRect(float x, float y, float w, float h, const Color& color, const DrawTransform& transform);

        // Draws a texture like DrawTexture() but rotated about @p transform's pivot and mirrored by its flips (the UVs swap). An identity transform is
        // DrawTexture() unchanged; otherwise the default draws a textured quad through DrawGeometry() (the whole texture when @p srcRect is null or
        // empty) and @p tint as the vertex color. A texture without size draws nothing. A backend can override it with a native draw.
        virtual void DrawTransformedTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH, const Color& tint, const DrawTransform& transform);

        // Makes a texture from pixels in memory: RGBA8 (red first), straight alpha, rows packed from the top, so
        // @p rgba holds `width * height * 4` bytes. The pixels are copied. Empty when the size is zero or the backend fails.
        // Main thread only, like every GPU call (a loader running on a pool thread must not call it).
        [[nodiscard]] virtual IntrusivePtr<ITexture> CreateTexture(const uint8_t* rgba, uint32_t width, uint32_t height) = 0;

        // Offscreen target shown via ImGui::Image: SetRenderTarget() before drawing the content, UnsetRenderTarget() after.
        // Only while no other render pass is active (before ClearScreenWidget opens the main pass).
        BE_FUNCTION virtual IntrusivePtr<IRenderTarget> CreateRenderTarget(uint32_t width, uint32_t height) = 0;
        BE_FUNCTION virtual void SetRenderTarget(IRenderTarget* target) = 0;
        BE_FUNCTION virtual void UnsetRenderTarget() = 0;

        // Size in pixels of the target being drawn into: the offscreen target between SetRenderTarget() and
        // UnsetRenderTarget(), else the back buffer while its pass is open. 0 before the first pass.
        virtual float GetOutputWidth() const = 0;
        virtual float GetOutputHeight() const = 0;

        // Asks for the image of the back buffer: @p callback gets it once, on the calling thread, just before the frame being drawn is
        // presented (from Present() or the RenderPresentEvent, not from here). A new request replaces one not yet served, whose callback gets
        // an error. The default, for a backend that cannot read its output back, calls @p callback at once with an error.
        virtual void RequestCapture(CaptureCallback callback);

        // How the draws combine with the target (Alpha by default). State of the interface, like the view transform; a backend may ignore a
        // mode (BlendMode.h). Whoever sets another mode puts the previous one back (ScopedBlendMode), so no mode leaks into a later draw.
        void SetBlendMode(BlendMode mode) {
            _blendMode = mode;
        }
        [[nodiscard]] BlendMode GetBlendMode() const {
            return _blendMode;
        }

        // The opacity of what is drawn next, 0 to 1 (a NaN is 1, a value outside the range is cut to it). State of the interface like the blend
        // mode: every primitive multiplies its alpha by it, once. It is a per-draw multiply, not a composite of a group, so overlapping draws
        // blend through each other. Whoever sets another value puts the previous one back (ScopedOpacity, which nests by multiplying).
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

        // Maps the coordinates of the draws to output pixels. The scene sets it around its draw pass (Scene::Draw()).
        void SetViewTransform(const ViewTransform& transform) {
            _viewTransform = transform;
        }
        const ViewTransform& GetViewTransform() const {
            return _viewTransform;
        }

        // Clips the draws to a rect in output pixels (after the view transform, so a scene transform does not move it). Rects nest: the clip is
        // the intersection of every pushed rect, and the pop restores the one before. An unbalanced push leaks into the next frame.
        void PushClipRect(const Rect& rect);
        void PopClipRect();

        // The current clip in output pixels, or nothing when no rect is pushed. A rect that does not overlap the one before it leaves an
        // empty clip, which hides everything (IsClipEmpty()).
        [[nodiscard]] Optional<Rect> GetClipRect() const;
        [[nodiscard]] bool IsClipEmpty() const;
        [[nodiscard]] size_t GetClipDepth() const {
            return _clipStack.size();
        }

        // The draws counted since the last call, and a fresh start (once per frame, FrameStatsCollector). A backend counts where a primitive
        // really reaches the GPU, so a draw dropped by a clip or a missing texture is not counted, and a composed draw is counted once.
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
        // Called after the clip changes (push or pop)
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
        Vector<Rect> _clipStack;
        RenderCounters _counters;
        const ITexture* _lastCountedTexture = nullptr;  // compared only, never dereferenced
        int _countersPaused = 0;
    };

    /// The GPU device of @p renderer, or the renderer's name and why it has none, for the caller to log once instead of drawing a blank screen
    [[nodiscard]] std::expected<IGpuDevice*, String> RequireGpuDevice(IRenderer& renderer);

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
