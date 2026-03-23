#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    // Renders the active scene into an offscreen texture and displays it as a
    // dockable ImGui window ("Viewport"). The offscreen pass happens BEFORE
    // ClearScreenWidget opens the swapchain render pass, so this widget must
    // appear before ClearScreenWidget in WidgetsConfig.xml.
    class ViewportWidget : public IWidget {
        BE_CLASS(ViewportWidget)
    public:
        ViewportWidget() = default;
        ~ViewportWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        BE_REFLECT_FIELD BECore::Color _clearColor{30, 30, 30, 255};
        IntrusivePtr<IRenderTarget> _renderTarget;
        uint32_t _lastWidth  = 0;
        uint32_t _lastHeight = 0;
    };

}  // namespace BECore
