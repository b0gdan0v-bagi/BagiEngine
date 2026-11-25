#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
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
    };

    /**
     * @brief The model of the Viewport's toolbar (an asset of the window kind `Bar`): it owns the preview choice and the input mode, and `ViewportWidget`
     * reads them through `IViewportToolbar`
     *
     * Registered in `ViewportToolbarRegistry` from `Initialize` for its lifetime. The stored choice comes with `EditorPreferencesChangedEvent` and is applied
     * without being told back; a change the user made is told once with `ViewportPreviewChangedEvent` or `ViewportInputModeChangedEvent` from the `Update`
     * that reads it. The size rules: a device brings the Device safe area, any other size drops it, Custom keeps the shape's other fields, and a stored
     * device this build lacks shows the row of its aspect. Command: `viewport.inputMode` flips the mode.
     */
    class ViewportToolbarModel : public IPanelModel, public SubscriptionHolder, public IViewportToolbar {
        BE_CLASS(ViewportToolbarModel)
    public:
        ViewportToolbarModel();

        [[nodiscard]] TypedRef GetObject() override;
        void Initialize() override;
        void Update() override;
        bool RunCommand(StringView id, StringView argument) override;

        [[nodiscard]] PreviewSettings GetPreview() const override;
        [[nodiscard]] ViewportInputMode GetInputMode() const override {
            return _inputMode;
        }

        [[nodiscard]] const ViewportToolbarView& GetView() const {
            return _view;
        }

    private:
        void OnPreferencesChanged(const EditorPreferencesChangedEvent& event);

        /// Takes what the controls wrote into the view: a combo row that moved applies its rules, the other fields are read as they stand
        void ReadView();
        /// Applies the choice of the size combo's row @p row; a separator or a row past the end changes nothing
        void ChooseSizeRow(int32_t row);
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
        int32_t _writtenSizeRow = 0;
        int32_t _writtenSafeAreaRow = 0;
        ViewportToolbarHandle _registration;
    };

}  // namespace BECore
