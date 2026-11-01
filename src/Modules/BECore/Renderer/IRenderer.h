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

    /// The image, or why it could not be taken
    using CaptureResult = std::expected<PixelBuffer, eastl::string>;
    using CaptureCallback = std::function<void(CaptureResult)>;

    class IRenderer : public RefCounted, public SubscriptionHolder {
        BE_CLASS(IRenderer, FACTORY_BASE)
    public:
        IRenderer() = default;
        ~IRenderer() override = default;

        BE_FUNCTION virtual bool Initialize(IMainWindow& window) = 0;
        BE_FUNCTION virtual void Destroy() = 0;
        BE_FUNCTION virtual void BeginFrame() = 0;
        BE_FUNCTION virtual void EndFrame() = 0;
        BE_FUNCTION virtual void Clear(const Color& color) = 0;
        BE_FUNCTION virtual void Present() = 0;
        BE_FUNCTION virtual void DrawFilledRect(float x, float y, float w, float h, const Color& color) = 0;
        BE_FUNCTION virtual void DrawTexture(ITexture& texture, const Rect* srcRect, float dstX, float dstY, float dstW, float dstH) = 0;

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

    private:
        ViewTransform _viewTransform;
    };

}  // namespace BECore
