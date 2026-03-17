#pragma once

namespace BECore {

    class IDeserializer;
    class IMainWindow;

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
    };

}  // namespace BECore
