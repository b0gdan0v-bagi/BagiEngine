#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/SceneView.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIPreview.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/PanelAssets/IPanelModel.h>
#include <Widgets/Viewport/ViewportInput.h>
#include <Widgets/Viewport/ViewportToolbarState.h>
#include <cstdint>

namespace BECore {

    /// A row of the size combo: an aspect, a device or Custom..., or a line between the groups
    struct ViewportSizeItem {
        BE_CLASS(ViewportSizeItem)

        BE_REFLECT_FIELD String text;
        BE_REFLECT_FIELD bool separator = false;
    };

    /// A canvas's switch in the Overlays popup: off hides its element rects, anchors, design rectangle and findings
    struct ViewportCanvasRow {
        BE_CLASS(ViewportCanvasRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD bool enabled = true;
    };

    /// A device whose shape outlines the selected element over the image
    struct ViewportDeviceRow {
        BE_CLASS(ViewportDeviceRow)

        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD bool checked = false;
    };

    /**
     * @brief What `ViewportToolbarPanel.xml` binds to: the Viewport's choices as rows, indices and bools
     *
     * The controls write the fields themselves; `ViewportToolbarModel::Update` reads them back. A combo's row is a choice only when it differs from the
     * row the model last wrote, so the model's own rewrite after a preferences event is never taken for the user's.
     */
    struct ViewportToolbarView {
        BE_CLASS(ViewportToolbarView)

        /// The row of `sizeItems` the shape is: the aspects, a separator, the devices of `UIPreview::GetDevices()`, a separator, Custom...
        BE_REFLECT_FIELD int32_t sizeRow = 0;
        BE_REFLECT_FIELD Vector<ViewportSizeItem> sizeItems;
        /// The shape is a custom size: W and H are shown
        BE_REFLECT_FIELD bool isCustom = false;
        /// The shape is Free (not custom, no device, aspect Free), which Rotate does not apply to
        BE_REFLECT_FIELD bool sizeFree = true;
        BE_REFLECT_FIELD uint32_t customWidth = PreviewShape{}.customWidth;
        BE_REFLECT_FIELD uint32_t customHeight = PreviewShape{}.customHeight;
        BE_REFLECT_FIELD bool rotated = false;
        /// The row of `safeAreaItems`, in the order of `SafeAreaSimulation`
        BE_REFLECT_FIELD int32_t safeAreaRow = 0;
        BE_REFLECT_FIELD Vector<String> safeAreaItems;
        BE_REFLECT_FIELD bool overlaySafeArea = PreviewOverlays{}.safeArea;
        BE_REFLECT_FIELD bool overlayViewBounds = false;
        BE_REFLECT_FIELD bool overlayCanvasBounds = false;
        BE_REFLECT_FIELD bool overlayTouchTargets = false;
        BE_REFLECT_FIELD bool overlayRegions = false;
        BE_REFLECT_FIELD bool overlayElementRects = false;
        BE_REFLECT_FIELD bool overlayAnchors = false;
        BE_REFLECT_FIELD bool overlayDesignRect = false;
        BE_REFLECT_FIELD bool overlayFindings = false;
        BE_REFLECT_FIELD ViewportInputMode inputMode = ViewportInputMode::Play;
        /// There is an active scene (the "View: ..." text)
        BE_REFLECT_FIELD bool hasScene = false;
        /// The active scene has view variants (the view combo)
        BE_REFLECT_FIELD bool hasViews = false;
        /// The row of `viewItems`: Auto, Default, then each variant of the active scene
        BE_REFLECT_FIELD int32_t viewRow = 0;
        BE_REFLECT_FIELD Vector<String> viewItems;
        /// The view the scene drew with last: its name and size
        BE_REFLECT_FIELD String viewText;
        /// The size of the image on the screen, as `PublishCamera` gave it
        BE_REFLECT_FIELD String zoomText;
        /// The editor camera left its fit (Reset is enabled)
        BE_REFLECT_FIELD bool cameraMoved = false;
        /// The canvases the viewport's pass last published, each with its switch
        BE_REFLECT_FIELD Vector<ViewportCanvasRow> canvases;
        BE_REFLECT_FIELD bool hasCanvases = false;
        /// The shapes of the checked devices are laid out in landscape
        BE_REFLECT_FIELD bool landscape = false;
        /// One row per device of `UIPreview::GetDevices()` (at most `kMaxShapeDevices`)
        BE_REFLECT_FIELD Vector<ViewportDeviceRow> devices;
        /// The gizmo's tool and space (the buttons run commands) and its snap switches and steps (the Snap popup writes them)
        BE_REFLECT_FIELD Gizmo::Tool tool = Gizmo::Tool::Translate;
        BE_REFLECT_FIELD Gizmo::Space space = Gizmo::Space::World;
        BE_REFLECT_FIELD bool snapPosition = false;
        BE_REFLECT_FIELD bool snapAngle = false;
        BE_REFLECT_FIELD bool snapScale = false;
        BE_REFLECT_FIELD float positionStep = Gizmo::kDefaultPositionStep;
        BE_REFLECT_FIELD float angleStep = Gizmo::kDefaultAngleStep;
        BE_REFLECT_FIELD float scaleStep = Gizmo::kDefaultScaleStep;
    };

    /**
     * @brief The model of the Viewport's toolbar (an asset of the window kind `Bar`): it owns the preview choice and the input mode, and `ViewportWidget`
     * reads them through `IViewportToolbar`
     *
     * Registered in `ViewportToolbarRegistry` from `Initialize` for its lifetime. The stored choice comes with `EditorPreferencesChangedEvent` and is applied
     * without being told back; a change the user made is told once with `ViewportPreviewChangedEvent` or `ViewportInputModeChangedEvent` from the `Update`
     * that reads it. The size rules: a device brings the Device safe area, any other size drops it, Custom keeps the shape's other fields, and a stored
     * device this build lacks shows the row of its aspect. The forced view is session state, kept with the name of the scene it was chosen in and dropped
     * (Auto) when another scene is active or the variant is gone. Commands: `viewport.inputMode` flips the mode; `viewport.tool.translate|rotate|scale`,
     * `viewport.gizmoSpace` and `viewport.snap` (all three snap switches off when any is on, otherwise all on) set the gizmo's choices, which are session state
     * like the keys that reach them through `SetTool` and `ToggleSpace`. The rows, the view text and the zoom text are
     * made again only when what they show changed, so a steady `Update` allocates nothing. The per-canvas switches and the checked devices are session state,
     * never stored; a canvas switched off is kept by its node (compared, never dereferenced) until a canvas list of another scene comes.
     */
    class ViewportToolbarModel : public IPanelModel, public SubscriptionHolder, public IViewportToolbar {
        BE_CLASS(ViewportToolbarModel)
    public:
        ViewportToolbarModel();

        [[nodiscard]] TypedRef GetObject() override;
        void Initialize() override;
        void Update() override;
        /// Reads what the bar's controls wrote in this frame's draw, so the viewport, which reads the choices after it draws the bar, uses them in the same frame
        void EndUpdate() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] PreviewSettings GetPreview() const override;
        [[nodiscard]] ViewportInputMode GetInputMode() const override {
            return _inputMode;
        }
        [[nodiscard]] Optional<int32_t> GetForcedView(const Scene* scene) const override;
        void PublishCamera(float zoomPercent, bool moved) override;
        void PublishCanvases(const Scene* scene, Span<const CanvasSwitchRow> canvases) override;
        [[nodiscard]] bool IsCanvasEnabled(const SceneNode* node) const override;
        [[nodiscard]] uint64_t GetShapeMask() const override;
        [[nodiscard]] bool IsLandscapeShapes() const override {
            return _view.landscape;
        }
        void ClearFindingsOverlay() override;
        [[nodiscard]] ViewportGizmoSettings GetGizmoSettings() const override {
            return _gizmo;
        }
        void SetTool(Gizmo::Tool tool) override;
        void ToggleSpace() override;

        [[nodiscard]] const ViewportToolbarView& GetView() const {
            return _view;
        }

    private:
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);

        /// Takes the view, makes the rows of the active scene again when they moved and tells the editor what changed
        void Sync();

        /// Takes what the controls wrote into the view: a combo row that moved applies its rules, the other fields are read as they stand. @p scene is the
        /// active scene, whose variants the view combo lists
        void ReadView(const Scene* scene);
        /// Applies the choice of the size combo's row @p row; a separator or a row past the end changes nothing
        void ChooseSizeRow(int32_t row);
        /// Applies the choice of the view combo's row @p row in @p scene; a row past its variants changes nothing
        void ChooseViewRow(const Scene* scene, int32_t row);
        /// The rows of the view combo and the view text of @p scene, made again when the scene, a variant's name or the active view moved
        void SyncScene(const Scene* scene);
        /// Takes the canvas switches the user wrote into the rows of the canvases last published
        void ReadCanvasRows();
        /// Writes the state into the view and notes the rows written
        void WriteView();
        /// Tells the editor what differs from what it last stored or was told
        void EmitChanges();

        ViewportToolbarView _view;
        PreviewShape _shape;
        SafeAreaSimulation _safeArea = SafeAreaSimulation::None;
        PreviewOverlays _overlays;
        ViewportInputMode _inputMode = ViewportInputMode::Play;
        PreviewSettings _storedPreview;  // what the editor last stored (applied from a preferences event, or told by us)
        ViewportInputMode _storedInputMode = ViewportInputMode::Play;
        ViewportGizmoSettings _gizmo;
        int32_t _writtenSizeRow = 0;
        int32_t _writtenSafeAreaRow = 0;
        Optional<int32_t> _forcedView;  // the forced variant, -1 the default view, nullopt Auto
        PoolString _forcedViewScene;    // the scene the choice was made in
        int32_t _writtenViewRow = 0;
        // What the view's scene part was made for: the scene, the names of its variants, the view it drew with
        const Scene* _itemsScene = nullptr;  // compared, never dereferenced
        Vector<PoolString> _itemsNames;
        bool _hasTextView = false;
        ActiveSceneView _textView;
        Optional<int32_t> _zoomShown;             // the rounded percent zoomText shows
        const Scene* _canvasScene = nullptr;      // the scene of the last published canvases, compared only
        Vector<CanvasSwitchRow> _canvasList;      // the canvases the rows of `canvases` show
        Vector<const SceneNode*> _disabledNodes;  // the canvases switched off, compared with the published ones, never dereferenced
        ViewportToolbarHandle _registration;
    };

}  // namespace BECore
