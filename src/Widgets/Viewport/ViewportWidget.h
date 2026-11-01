#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Widgets/IWidget.h>

namespace BECore {

    class IDeserializer;

    // Renders the active scene into an offscreen texture and displays it as a
    // dockable ImGui window ("Viewport"). The offscreen pass happens BEFORE
    // ClearScreenWidget opens the swapchain render pass, so this widget must
    // appear before ClearScreenWidget in WidgetsConfig.xml.
    //
    // A toolbar forces the image to a preset shape (16:9, 4:3, 21:9, 9:16) and pretends a safe area (a notch, side
    // insets), so a UI layout can be looked at in several shapes without resizing the window (UIPreview). The
    // viewport is not the window: the safe area that canvases see during the scene pass is the simulated one.
    class ViewportWidget : public IWidget {
        BE_CLASS(ViewportWidget)
    public:
        ViewportWidget() = default;
        ~ViewportWidget() override = default;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        // The aspect and safe-area combos above the image
        void DrawPreviewToolbar();

        // The simulated safe area as an outline over the image, so it is clear what the canvases were given
        void DrawSafeAreaOutline(float minX, float minY, float maxX, float maxY) const;

        // Mouse input of the image as the scene's input: origin is the image's top-left corner on the screen
        void ForwardMouse(float originX, float originY, bool hovered);

        BE_REFLECT_FIELD BECore::Color _clearColor{30, 30, 30, 255};
        IntrusivePtr<IRenderTarget> _renderTarget;
        uint32_t _lastWidth = 0;
        uint32_t _lastHeight = 0;
        ViewportAspect _aspect = ViewportAspect::Free;
        SafeAreaSimulation _safeAreaSimulation = SafeAreaSimulation::None;
        bool _pressedInViewport[3] = {false, false, false};  // left, right, middle: pressed over the image and not yet released
    };

}  // namespace BECore
