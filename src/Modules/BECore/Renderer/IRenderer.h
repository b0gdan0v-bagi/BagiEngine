#pragma once

#include <BECore/Renderer/ViewTransform.h>

namespace BECore {

    class IDeserializer;
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

    protected:
        // Called after the clip changes (push or pop). A backend applies GetClipRect() to its next draws.
        virtual void OnClipRectChanged() {}

    private:
        static constexpr Color kWhite{255, 255, 255, 255};

        uint64_t _id = 0;
        bool _geometryWarned = false;
        ViewTransform _viewTransform;
        // Each entry is already intersected with the one below it
        eastl::vector<Rect> _clipStack;
    };

}  // namespace BECore
