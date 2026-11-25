#include "ViewportToolbarModel.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Generated/ViewportToolbarModel.gen.hpp>
#include <Widgets/Icons/IconsLucide.h>
#include <cmath>

namespace BECore {

    namespace {

        // In the order of the enums (ViewportAspect, SafeAreaSimulation). The icon names the control (the shape of the image, the safe area), so a combo
        // that shows the current value tells what it is
        constexpr const char* kAspectLabels[] = {ICON_LC_MAXIMIZE " Free", ICON_LC_PROPORTIONS " 16:9", ICON_LC_PROPORTIONS " 4:3", ICON_LC_PROPORTIONS " 21:9", ICON_LC_PROPORTIONS " 9:16"};
        constexpr const char* kSafeAreaLabels[] = {ICON_LC_SHIELD_CHECK " No safe area", ICON_LC_SHIELD_CHECK " Notch", ICON_LC_SHIELD_CHECK " Side insets", ICON_LC_SHIELD_CHECK " Device"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());
        constexpr const char* kCustomLabel = ICON_LC_RULER " Custom...";
        constexpr const char* kViewAutoLabel = ICON_LC_EYE " Auto";
        constexpr const char* kViewDefaultLabel = ICON_LC_EYE " Default";
        // The rows of the view combo before the variants: Auto, then the default view
        constexpr int32_t kViewRowAuto = 0;
        constexpr int32_t kViewRowDefault = 1;
        constexpr int32_t kViewRowFirstVariant = 2;
        // The switch of a canvas whose node has no name
        constexpr const char* kUnnamedCanvas = "(unnamed)";
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

        // The text of a variant in the view combo: its name, or its number when it has none
        String VariantLabel(const SceneViewVariant& variant, size_t index) {
            return variant.name.Empty() ? Format("{} #{}", ICON_LC_EYE, index) : Format("{} {}", ICON_LC_EYE, variant.name.ToStringView());
        }

        // The same nodes with the same names in the same order
        bool SameCanvases(Span<const CanvasSwitchRow> canvases, const Vector<CanvasSwitchRow>& kept) {
            if (canvases.size() != kept.size()) {
                return false;
            }
            for (size_t i = 0; i < canvases.size(); ++i) {
                if (canvases[i].node != kept[i].node || canvases[i].name != kept[i].name) {
                    return false;
                }
            }
            return true;
        }

        // The variant names of @p variants equal @p names
        bool SameNames(Span<const SceneViewVariant> variants, const Vector<PoolString>& names) {
            if (variants.size() != names.size()) {
                return false;
            }
            for (size_t i = 0; i < variants.size(); ++i) {
                if (variants[i].name != names[i]) {
                    return false;
                }
            }
            return true;
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
        const size_t shapeDevices = std::min(devices.size(), kMaxShapeDevices);
        _view.devices.resize(shapeDevices);
        for (size_t i = 0; i < shapeDevices; ++i) {
            _view.devices[i].name = String(devices[i].name.data(), devices[i].name.size());
        }
        _view.viewItems.push_back(kViewAutoLabel);
        _view.viewItems.push_back(kViewDefaultLabel);
        PublishCamera(0.0f, false);
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
        const SceneManager* scenes = CoreManager::FindSceneManager();
        const Scene* scene = scenes != nullptr ? scenes->GetActiveScene() : nullptr;
        ReadView(scene);
        ReadCanvasRows();
        if (_forcedView && !GetForcedView(scene)) {
            _forcedView = std::nullopt;
        }
        SyncScene(scene);
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

    Optional<int32_t> ViewportToolbarModel::GetForcedView(const Scene* scene) const {
        if (!_forcedView || scene == nullptr || scene->GetName() != _forcedViewScene) {
            return std::nullopt;
        }
        const int32_t variants = static_cast<int32_t>(scene->GetViewVariants().size());
        return *_forcedView >= -1 && *_forcedView < variants ? _forcedView : std::nullopt;
    }

    void ViewportToolbarModel::PublishCamera(float zoomPercent, bool moved) {
        _view.cameraMoved = moved;
        const int32_t shown = static_cast<int32_t>(std::lround(zoomPercent));
        if (_zoomShown != shown) {
            _zoomShown = shown;
            _view.zoomText = Format("{:3}%", shown);
        }
    }

    void ViewportToolbarModel::PublishCanvases(const Scene* scene, Span<const CanvasSwitchRow> canvases) {
        // A switch clicked in this frame's draw counts before the rows may be made again for the new list
        ReadCanvasRows();
        if (scene != _canvasScene) {
            _canvasScene = scene;
            _disabledNodes.clear();
        } else if (SameCanvases(canvases, _canvasList)) {
            return;
        }
        _canvasList.assign(canvases.begin(), canvases.end());
        _view.canvases.resize(canvases.size());
        for (size_t i = 0; i < canvases.size(); ++i) {
            ViewportCanvasRow& row = _view.canvases[i];
            const PoolString name = canvases[i].name;
            row.name = name.Empty() ? String(kUnnamedCanvas) : String(name.ToStringView().data(), name.ToStringView().size());
            row.enabled = IsCanvasEnabled(canvases[i].node);
        }
        _view.hasCanvases = !canvases.empty();
    }

    bool ViewportToolbarModel::IsCanvasEnabled(const SceneNode* node) const {
        return std::find(_disabledNodes.begin(), _disabledNodes.end(), node) == _disabledNodes.end();
    }

    uint64_t ViewportToolbarModel::GetShapeMask() const {
        uint64_t mask = 0;
        for (size_t i = 0; i < _view.devices.size(); ++i) {
            if (_view.devices[i].checked) {
                mask |= uint64_t{1} << i;
            }
        }
        return mask;
    }

    void ViewportToolbarModel::ReadCanvasRows() {
        const size_t count = std::min(_view.canvases.size(), _canvasList.size());
        for (size_t i = 0; i < count; ++i) {
            const SceneNode* node = _canvasList[i].node;
            const auto found = std::find(_disabledNodes.begin(), _disabledNodes.end(), node);
            const bool wasEnabled = found == _disabledNodes.end();
            if (_view.canvases[i].enabled == wasEnabled) {
                continue;
            }
            if (wasEnabled) {
                _disabledNodes.push_back(node);
            } else {
                _disabledNodes.erase(found);
            }
        }
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

    void ViewportToolbarModel::ReadView(const Scene* scene) {
        if (_view.sizeRow != _writtenSizeRow) {
            ChooseSizeRow(_view.sizeRow);
        }
        if (_view.viewRow != _writtenViewRow) {
            ChooseViewRow(scene, _view.viewRow);
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

    void ViewportToolbarModel::ChooseViewRow(const Scene* scene, int32_t row) {
        if (row == kViewRowAuto) {
            _forcedView = std::nullopt;
            return;
        }
        if (scene == nullptr || row < kViewRowDefault || row >= kViewRowFirstVariant + static_cast<int32_t>(scene->GetViewVariants().size())) {
            return;
        }
        _forcedView = row - kViewRowFirstVariant;
        _forcedViewScene = scene->GetName();
    }

    void ViewportToolbarModel::SyncScene(const Scene* scene) {
        _view.hasScene = scene != nullptr;
        const Span<const SceneViewVariant> variants = scene != nullptr ? scene->GetViewVariants() : Span<const SceneViewVariant>{};
        _view.hasViews = !variants.empty();
        if (scene != _itemsScene || !SameNames(variants, _itemsNames)) {
            _itemsScene = scene;
            _itemsNames.clear();
            _view.viewItems.resize(static_cast<size_t>(kViewRowFirstVariant));
            for (size_t i = 0; i < variants.size(); ++i) {
                _itemsNames.push_back(variants[i].name);
                _view.viewItems.push_back(VariantLabel(variants[i], i));
            }
        }

        if (scene == nullptr) {
            _hasTextView = false;
            _view.viewText.clear();
            return;
        }
        // The view the scene drew with last, picked by the size of the image or forced by the combo
        const ActiveSceneView active = scene->GetActiveView();
        if (!_hasTextView || !(active == _textView)) {
            _hasTextView = true;
            _textView = active;
            const StringView name = active.index < 0 ? StringView("default") : active.name.ToStringView();
            _view.viewText = Format("View: {} {:.0f} x {:.0f}", name, active.width, active.height);
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
        _writtenViewRow = _forcedView ? kViewRowFirstVariant + *_forcedView : kViewRowAuto;
        _view.viewRow = _writtenViewRow;
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
