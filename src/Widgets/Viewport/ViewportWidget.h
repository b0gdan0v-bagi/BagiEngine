#pragma once

#include <BECore/Renderer/IRenderTarget.h>
#include <BECore/UI/UIPreview.h>
#include <BECore/Widgets/IWidget.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <Widgets/Viewport/ViewportInput.h>

namespace BECore {

    class IDeserializer;
    class Scene;
    class SceneNode;

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
    // The image is covered by an InvisibleButton (a real item with an id: a press on it is always seen and never moves the window)
    // and its mouse is the game's input as the window's (ViewportPointer). The input mode (ViewportInputMode, a toolbar toggle and
    // the viewport.inputMode command) says whether a press goes to the game (Play) or is kept for the editor (Edit: the game gets
    // nothing and ViewportEditClickEvent is emitted); Alt held flips it for one click. A badge in the image corner shows the
    // effective mode, and Edit adds an outline around the image. A left click in Edit is answered by the scene editor (ScenePick), and the nodes it
    // selects, from here or from the hierarchy (SceneSelectionChangedEvent), are outlined over the image (the primary in the accent colour).
    // In Edit the image has an editor camera (ViewportCamera: wheel zoom about the pointer, middle-drag or Space + left-drag pan, F frames the selection, a Reset
    // button and a zoom label): the texture is drawn scaled and shifted under the clip rect of the panel, and everything in target pixels goes through ViewportFrame.
    // A drag on empty ground draws a rubber band and selects what it touches; the gizmo (ViewportGizmos) moves, turns and scales the selected nodes (keys W / E / R / X while the panel has the keys;
    // the commands viewport.tool.*, viewport.gizmoSpace, viewport.snap and viewport.frameSelected do the same from the palette, with no default chord).
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

        // The grid over the image: a line every _gridStep target pixels from its corner, clipped to the panel and to the target
        void DrawGrid(const ViewportFrame& frame) const;

        // The command of the input mode (viewport.inputMode) flips it
        void OnCommand(const EditorCommandEvent& event);

        // Tells EditorLayoutWidget (ViewportInputModeChangedEvent) when the mode differs from what it was last told or told us
        void EmitInputModeIfChanged();

        // Tells EditorLayoutWidget (ViewportPreviewChangedEvent) when the size, rotation, safe area or an overlay differs from what it was last told or told us (not while a custom W / H field is
        // being typed in)
        void EmitPreviewIfChanged();

        // The size picker (with W / H for a custom size), the rotate toggle, the safe-area and view combos above the image
        void DrawPreviewToolbar();

        // The size combo: Free, the aspect presets, the devices, Custom...; keeps the safe-area choice consistent with the size
        void DrawSizeCombo();

        // The text the size combo shows (the icon and the label of _shape), made again only when the shape changes
        const char* SizePreview();

        // The view combo of the active scene (nothing for a scene without variants); keeps the choice as an index
        void DrawViewCombo(const Scene& scene);

        // Falls back to Auto when the active scene is not the one the choice was made for or the index does not exist in it
        void ValidateForcedView(const Scene* scene);

        // The "Overlays" button and its popup of checkboxes (_overlays)
        void DrawOverlaysPopup();

        // The overlays over the image, in target pixels through @p frame and clipped to the panel: the view bounds (bars tinted), the bounds of
        // every visible canvas, the regions (named outlines, under the touch targets; the name and the canvas-unit rect on hover), the touch targets
        // (red under UISettings::minTouchSize, the element name on hover, which wins over a region), the safe area. The canvases
        // are the ones the pass just laid out for this target. @p outputSize is the target's size, @p hovered the pointer is over the image
        /// The "Paused" mark in the top-left corner of the image while the game is paused (CoreManager::GetPause())
        void DrawPausedMark(const ImVec2& imageMin, const ImVec2& imageMax) const;
        void DrawOverlays(Scene* scene, const ViewportFrame& frame, const ImVec2& outputSize, const Rect& safeArea, bool hovered) const;

        // The simulated safe area (in target pixels) as an outline over the image, so it is clear what the canvases were given
        void DrawSafeAreaOutline(const ViewportFrame& frame, const Rect& safeArea) const;

        // Keeps the nodes the scene editor selected (raw pointers, only compared until DrawSelectionOutlines finds each node alive)
        void OnSelectionChanged(const SceneSelectionChangedEvent& event);

        // The outline of every selected node (ScenePick::SelectionOutlines: its rect and, for a canvas, the canvas bounds) over the image, the primary (the last) in the accent colour and the
        // others in its muted one, clipped to the panel. Nothing for no selection, another scene, or a node that is no longer in the tree of @p scene; a node hidden by the editor flag is outlined too
        void DrawSelectionOutlines(Scene* scene, const ViewportFrame& frame) const;

        // The editor camera: the zoom label and the Reset button next to the input mode toggle, and F (frame the selection, or reset when nothing is selected)
        void DrawCameraControls();
        void FrameSelection(Scene* scene, const ImVec2& panelSize, float baseZoom);

        // The Play / Edit toggle of the toolbar
        void DrawInputModeToggle();

        // Mouse input of the image as the scene's input, or an edit click (with the selection keys held): @p frame says where the image is, @p hovered is the hover of the
        // hit area, @p pressTaken that a pan or a gizmo took this frame's press, which then is no edit click
        void ForwardMouse(const ViewportFrame& frame, bool hovered, bool pressTaken);

        // The badge of the effective input mode in the top-right corner of the image, and in Edit an outline around the image
        void DrawInputModeMark(const ImVec2& imageMin, const ImVec2& imageMax, ViewportInputTarget target) const;

        BE_REFLECT_FIELD BECore::Color _clearColor{30, 30, 30, 255};
        IntrusivePtr<IRenderTarget> _renderTarget;
        uint32_t _lastWidth = 0;
        uint32_t _lastHeight = 0;
        PreviewShape _shape;
        SafeAreaSimulation _safeAreaSimulation = SafeAreaSimulation::None;
        PreviewOverlays _overlays;
        PreviewSettings _storedPreview;  // the choice the editor last stored (applied from a preferences event, or sent by us)
        Optional<int32_t> _forcedView;   // the index of the forced view variant, -1 the default, nullopt Auto
        PoolString _forcedViewScene;     // the scene the choice was made for
        bool _editingSize = false;       // a custom W / H field is being typed in: the shape is applied live, the editor is told when the field is left
        eastl::string _sizePreview;
        PreviewShape _sizePreviewShape;  // the shape _sizePreview was made for
        bool _sizePreviewValid = false;
        eastl::string _viewPreview;  // the view combo's text of a forced variant
        int _viewPreviewRow = -1;    // the row and the name _viewPreview was made for
        PoolString _viewPreviewName;
        bool _gridEnabled = false;
        int32_t _gridStep = 0;
        const Scene* _selectedScene = nullptr;     // the selection last told by SceneSelectionChangedEvent: a node is never dereferenced before it is found in the active scene
        eastl::vector<SceneNode*> _selectedNodes;  // in selection order, the primary last
        ViewportCamera _camera;                    // the editor camera; the image is drawn with it while the input mode is Edit
        ViewportCameraInput _cameraInput;
        ViewportGizmos _gizmos;
        bool _frameRequested = false;  // viewport.frameSelected, answered in the next Update (it needs the panel size)
        ViewportBoxSelect _boxSelect;
        float _shownScale = 1.0f;  // screen units per target pixel of the last frame, for the zoom label
        ViewportPointer _pointer;  // the held presses and where each goes
        ViewportInputMode _inputMode = ViewportInputMode::Play;
        ViewportInputMode _storedInputMode = ViewportInputMode::Play;  // the mode the editor last stored (applied from a preferences event, or sent by us)
    };

}  // namespace BECore
