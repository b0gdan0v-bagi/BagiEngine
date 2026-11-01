#pragma once

#include <BECore/Renderer/ViewTransform.h>

namespace BECore {

    class IDeserializer;
    class IMainWindow;
    class ITexture;
    class IRenderTarget;

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
