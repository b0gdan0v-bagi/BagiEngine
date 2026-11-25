#include "ViewportToolbarModel.h"

#include <Generated/ViewportToolbarModel.gen.hpp>
#include <Widgets/Icons/IconsLucide.h>

namespace BECore {

    namespace {

        // In the order of the enums (ViewportAspect, SafeAreaSimulation). The icon names the control (the shape of the image, the safe area), so a combo
        // that shows the current value tells what it is
        constexpr const char* kAspectLabels[] = {ICON_LC_MAXIMIZE " Free", ICON_LC_PROPORTIONS " 16:9", ICON_LC_PROPORTIONS " 4:3", ICON_LC_PROPORTIONS " 21:9", ICON_LC_PROPORTIONS " 9:16"};
        constexpr const char* kSafeAreaLabels[] = {ICON_LC_SHIELD_CHECK " No safe area", ICON_LC_SHIELD_CHECK " Notch", ICON_LC_SHIELD_CHECK " Side insets", ICON_LC_SHIELD_CHECK " Device"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());
        constexpr const char* kCustomLabel = ICON_LC_RULER " Custom...";
        // A device at least this wide (logical points, portrait) is shown with the tablet icon
        constexpr uint32_t kTabletMinWidth = 600;

        // The rows of the size combo: the aspects, a separator, the devices, a separator, Custom...
        constexpr int32_t kAspectRows = static_cast<int32_t>(std::size(kAspectLabels));
        constexpr int32_t kFirstDeviceRow = kAspectRows + 1;

        int32_t DeviceCount() {
            return static_cast<int32_t>(UIPreview::GetDevices().size());
        }

        int32_t CustomRow() {
            return kFirstDeviceRow + DeviceCount() + 1;
        }

        // The icon and the name of a device in the size combo
        String DeviceLabel(const PreviewDevice& device) {
            return Format("{} {} ({}x{})", device.width >= kTabletMinWidth ? ICON_LC_TABLET : ICON_LC_SMARTPHONE, device.name, device.width, device.height);
        }

        // A device of this build, so the Device safe area and the device row apply; not for a custom shape or an index this build lacks
        bool HasDevice(const PreviewShape& shape) {
            return !shape.custom && shape.device >= 0 && shape.device < DeviceCount();
        }

        int32_t SizeRowOf(const PreviewShape& shape) {
            if (shape.custom) {
                return CustomRow();
            }
            if (HasDevice(shape)) {
                return kFirstDeviceRow + shape.device;
            }
            return static_cast<int32_t>(shape.aspect);
        }

        bool IsFree(const PreviewShape& shape) {
            return !shape.custom && !HasDevice(shape) && shape.aspect == ViewportAspect::Free;
        }

    }  // namespace

    ViewportToolbarModel::ViewportToolbarModel() {
        const Span<const PreviewDevice> devices = UIPreview::GetDevices();
        _view.sizeItems.reserve(static_cast<size_t>(CustomRow()) + 1);
        const auto addRow = [this](String text, bool separator) {
            ViewportSizeItem& item = _view.sizeItems.emplace_back();
            item.text = std::move(text);
            item.separator = separator;
        };
        for (const char* label : kAspectLabels) {
            addRow(label, false);
        }
        addRow({}, true);
        for (const PreviewDevice& device : devices) {
            addRow(DeviceLabel(device), false);
        }
        addRow({}, true);
        addRow(kCustomLabel, false);
        for (const char* label : kSafeAreaLabels) {
            _view.safeAreaItems.push_back(label);
        }
        WriteView();
    }

    TypedRef ViewportToolbarModel::GetObject() {
        return TypedRef::Of(_view);
    }

    void ViewportToolbarModel::Initialize() {
        Subscribe<EditorPreferencesChangedEvent, &ViewportToolbarModel::OnPreferencesChanged>(this);
        _registration = ViewportToolbarRegistry::GetInstance().Register(*this);
    }

    void ViewportToolbarModel::Update() {
        ReadView();
        WriteView();
        EmitChanges();
    }

    bool ViewportToolbarModel::RunCommand(StringView id, StringView /*argument*/) {
        if (id != EditorCommandIds::kViewportInputMode.View()) {
            return false;
        }
        _inputMode = _inputMode == ViewportInputMode::Play ? ViewportInputMode::Edit : ViewportInputMode::Play;
        WriteView();
        return true;
    }

    PreviewSettings ViewportToolbarModel::GetPreview() const {
        return {_shape, _safeArea, _overlays};
    }

    void ViewportToolbarModel::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        // The stored choice is also what the editor was last told, so applying it tells nothing back
        _storedPreview = event.GetPreview();
        _shape = _storedPreview.shape;
        _safeArea = _storedPreview.safeArea;
        _overlays = _storedPreview.overlays;
        _storedInputMode = event.viewportInputMode;
        _inputMode = _storedInputMode;
        // The view must show it before the next Update reads the view back, or the old fields would be taken for the user's
        WriteView();
    }

    void ViewportToolbarModel::ReadView() {
        if (_view.sizeRow != _writtenSizeRow) {
            ChooseSizeRow(_view.sizeRow);
        }
        if (_view.safeAreaRow != _writtenSafeAreaRow && _view.safeAreaRow >= 0 && static_cast<size_t>(_view.safeAreaRow) < EnumUtils<SafeAreaSimulation>::Count()) {
            _safeArea = static_cast<SafeAreaSimulation>(_view.safeAreaRow);
        }
        _shape.customWidth = _view.customWidth;
        _shape.customHeight = _view.customHeight;
        _shape.rotated = _view.rotated;
        _overlays.safeArea = _view.overlaySafeArea;
        _overlays.viewBounds = _view.overlayViewBounds;
        _overlays.canvasBounds = _view.overlayCanvasBounds;
        _overlays.touchTargets = _view.overlayTouchTargets;
        _overlays.regions = _view.overlayRegions;
        _overlays.elementRects = _view.overlayElementRects;
        _overlays.anchors = _view.overlayAnchors;
        _overlays.designRect = _view.overlayDesignRect;
        _overlays.findings = _view.overlayFindings;
        _inputMode = _view.inputMode;
    }

    void ViewportToolbarModel::ChooseSizeRow(int32_t row) {
        // A device brings its own safe area; any other size drops it. The user can pick another simulation after either
        const auto dropDeviceSafeArea = [this] {
            if (_safeArea == SafeAreaSimulation::Device) {
                _safeArea = SafeAreaSimulation::None;
            }
        };
        if (row >= 0 && row < kAspectRows) {
            _shape.aspect = static_cast<ViewportAspect>(row);
            _shape.device = -1;
            _shape.custom = false;
            dropDeviceSafeArea();
        } else if (row >= kFirstDeviceRow && row < kFirstDeviceRow + DeviceCount()) {
            _shape.device = row - kFirstDeviceRow;
            _shape.custom = false;
            _safeArea = SafeAreaSimulation::Device;
        } else if (row == CustomRow()) {
            _shape.custom = true;
            dropDeviceSafeArea();
        }
    }

    void ViewportToolbarModel::WriteView() {
        _writtenSizeRow = SizeRowOf(_shape);
        _writtenSafeAreaRow = static_cast<int32_t>(_safeArea);
        _view.sizeRow = _writtenSizeRow;
        _view.isCustom = _shape.custom;
        _view.sizeFree = IsFree(_shape);
        _view.customWidth = _shape.customWidth;
        _view.customHeight = _shape.customHeight;
        _view.rotated = _shape.rotated;
        _view.safeAreaRow = _writtenSafeAreaRow;
        _view.overlaySafeArea = _overlays.safeArea;
        _view.overlayViewBounds = _overlays.viewBounds;
        _view.overlayCanvasBounds = _overlays.canvasBounds;
        _view.overlayTouchTargets = _overlays.touchTargets;
        _view.overlayRegions = _overlays.regions;
        _view.overlayElementRects = _overlays.elementRects;
        _view.overlayAnchors = _overlays.anchors;
        _view.overlayDesignRect = _overlays.designRect;
        _view.overlayFindings = _overlays.findings;
        _view.inputMode = _inputMode;
    }

    void ViewportToolbarModel::EmitChanges() {
        const PreviewSettings current = GetPreview();
        if (!(current == _storedPreview)) {
            _storedPreview = current;
            ViewportPreviewChangedEvent::Emit(current);
        }
        if (_inputMode != _storedInputMode) {
            _storedInputMode = _inputMode;
            ViewportInputModeChangedEvent::Emit(_inputMode);
        }
    }

}  // namespace BECore
