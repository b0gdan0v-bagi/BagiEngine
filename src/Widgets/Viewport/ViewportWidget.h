#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>

struct ImVec2;

namespace BECore {

    class IDeserializer;
    class Scene;

    // Renders the active scene into an offscreen texture and displays it as a
    // dockable ImGui window ("Viewport"). The offscreen pass happens BEFORE
    // ClearScreenWidget opens the swapchain render pass, so this widget must
    // appear before ClearScreenWidget in WidgetsConfig.xml.
    //
    // A toolbar picks the shape of the image (the panel, a preset ratio, a device of UIPreview::GetDevices() or a custom
    // W x H; "Rotate" turns it sideways) and pretends a safe area (a notch, side insets, the cut-out of the chosen device),
    // so a UI layout can be looked at in several shapes without resizing the window (UIPreview). The viewport is not the
    // window: the safe area that canvases see during the scene pass is the simulated one, and a device or custom size
    // renders the target at exactly that size (canvases lay out for it) and shows it scaled down to fit the panel, never
    // above 100%; the mouse is mapped back through that zoom.
    // An "Overlays" popup toggles what is drawn over the image: the safe area, the view bounds, the canvas bounds, the touch targets
    // and the regions a game published for its canvases (UIPreviewOverlay; the touch rule is the validator's).
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
        // The grid preference and the stored preview choice of the editor (EditorLayoutWidget sends them on its first frame and on every change)
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);

        // The grid over the image: a line every _gridStep image pixels (zoom screen units per pixel), clipped to the image
        void DrawGrid(float minX, float minY, float maxX, float maxY, float zoom) const;

        // Tells EditorLayoutWidget (ViewportPreviewChangedEvent) when the size, rotation, safe area or an overlay differs from what it was last told or told us (not while a custom W / H field is
        // being typed in)
        void EmitPreviewIfChanged();

        // The size picker (with W / H for a custom size), the rotate toggle, the safe-area and view combos above the image
        void DrawPreviewToolbar();

        // The size combo: Free, the aspect presets, the devices, Custom...; keeps the safe-area choice consistent with the size
        void DrawSizeCombo();

        // The view combo of the active scene (nothing for a scene without variants); keeps the choice as an index
        void DrawViewCombo(const Scene& scene);

        // Falls back to Auto when the active scene is not the one the choice was made for or the index does not exist in it
        void ValidateForcedView(const Scene* scene);

        // The "Overlays" button and its popup of checkboxes (_overlays)
        void DrawOverlaysPopup();

        // The overlays over the image, in target pixels scaled by zoom and clipped to the image: the view bounds (bars tinted), the bounds of
        // every visible canvas, the regions (named outlines, under the touch targets; the name and the canvas-unit rect on hover), the touch targets
        // (red under UISettings::minTouchSize, the element name on hover, which wins over a region), the safe area. The canvases
        // are the ones the pass just laid out for this target. @p outputSize is the target's size, @p hovered the pointer is over the image
        void DrawOverlays(Scene* scene, const ImVec2& imageMin, const ImVec2& imageMax, float zoom, const ImVec2& outputSize, const Rect& safeArea, bool hovered) const;

        // The simulated safe area (in target pixels) as an outline over the image, so it is clear what the canvases were given
        void DrawSafeAreaOutline(float minX, float minY, float zoom, const Rect& safeArea) const;

        // Mouse input of the image as the scene's input: origin is the image's top-left corner on the screen, the image is
        // shown at `zoom` screen units per target pixel
        void ForwardMouse(float originX, float originY, float zoom, bool hovered);

        BE_REFLECT_FIELD BECore::Color _clearColor{30, 30, 30, 255};
        IntrusivePtr<IRenderTarget> _renderTarget;
        uint32_t _lastWidth = 0;
        uint32_t _lastHeight = 0;
        PreviewShape _shape;
        SafeAreaSimulation _safeAreaSimulation = SafeAreaSimulation::None;
        PreviewOverlays _overlays;
        PreviewSettings _storedPreview;      // the choice the editor last stored (applied from a preferences event, or sent by us)
        std::optional<int32_t> _forcedView;  // the index of the forced view variant, -1 the default, nullopt Auto
        PoolString _forcedViewScene;         // the scene the choice was made for
        bool _editingSize = false;           // a custom W / H field is being typed in: the shape is applied live, the editor is told when the field is left
        bool _gridEnabled = false;
        int32_t _gridStep = 0;
        bool _pressedInViewport[3] = {false, false, false};  // left, right, middle: pressed over the image and not yet released
    };

}  // namespace BECore
