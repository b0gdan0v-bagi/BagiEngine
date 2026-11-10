#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    class IDeserializer;
    class Scene;

    // Renders the active scene into an offscreen texture and displays it as a
    // dockable ImGui window ("Viewport"). The offscreen pass happens BEFORE
    // ClearScreenWidget opens the swapchain render pass, so this widget must
    // appear before ClearScreenWidget in WidgetsConfig.xml.
    //
    // A toolbar forces the image to a preset shape (16:9, 4:3, 21:9, 9:16) and pretends a safe area (a notch, side
    // insets), so a UI layout can be looked at in several shapes without resizing the window (UIPreview). The
    // viewport is not the window: the safe area that canvases see during the scene pass is the simulated one.
    // A third combo forces one of the scene's view variants (Scene::SetViewOverride) around the viewport's own pass only.
    class ViewportWidget : public IWidget {
        BE_CLASS(ViewportWidget)
    public:
        ViewportWidget() = default;
        /// Gives back what the viewport set globally (the simulated safe area) and releases the render target, so the widget can
        /// die in the middle of the run when the mode switches. A mouse button pressed in the viewport and not yet released at
        /// that moment is lost: no button-up reaches the scene.
        ~ViewportWidget() override;

        BE_FUNCTION bool Initialize(IDeserializer& deserializer) override;
        BE_FUNCTION void Update() override;
        BE_FUNCTION void Draw() override;

    private:
        // The grid preference of the editor (EditorLayoutWidget sends it on its first frame and on every change)
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);

        // The grid over the image: a line every _gridStep image pixels, clipped to the image
        void DrawGrid(float minX, float minY, float maxX, float maxY) const;

        // The aspect, safe-area and view combos above the image
        void DrawPreviewToolbar();

        // The view combo of the active scene (nothing for a scene without variants); keeps the choice as an index
        void DrawViewCombo(const Scene& scene);

        // Falls back to Auto when the active scene is not the one the choice was made for or the index does not exist in it
        void ValidateForcedView(const Scene* scene);

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
        std::optional<int32_t> _forcedView;  // the index of the forced view variant, -1 the default, nullopt Auto
        PoolString _forcedViewScene;         // the scene the choice was made for
        bool _gridEnabled = false;
        int32_t _gridStep = 0;
        bool _pressedInViewport[3] = {false, false, false};  // left, right, middle: pressed over the image and not yet released
    };

}  // namespace BECore
