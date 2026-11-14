#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <Generated/ViewportWidget.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kEditOutlineThickness = 2.0f;       // the accent outline of the image in the Edit input mode, in screen units
        constexpr float kSelectionOutlineThickness = 2.0f;  // the outline of the selected node, in screen units
        constexpr float kPercent = 100.0f;
        constexpr float kFrameMargin = 24.0f;  // the room around the selection that F leaves, in screen units
        constexpr const char* kPlayMark = ICON_LC_GAMEPAD_2 " Play";
        constexpr const char* kEditMark = ICON_LC_MOUSE_POINTER_2 " Edit";
        constexpr const char* kInputModeTooltip = "Where a click on the image goes: Play sends it to the game as the window would, Edit keeps it for the editor (the game gets "
                                                  "nothing). Hold Alt (Option) for the other one for one click. Hotkey: F8 by default";

        constexpr float kSizeComboWidth = 170.0f;
        constexpr float kSizeFieldWidth = 60.0f;
        constexpr float kSafeAreaComboWidth = 130.0f;
        constexpr float kViewComboWidth = 130.0f;
        // The combo rows before the variants: Auto, then the default view
        constexpr int kViewComboAuto = 0;
        constexpr int kViewComboDefault = 1;
        constexpr int kViewComboFirstVariant = 2;
        // The grid is not drawn when its lines would be closer than this on screen (a big target shown small)
        constexpr float kMinDrawnGridStep = 2.0f;
        constexpr ImU32 kGridColor = IM_COL32(255, 255, 255, 40);
        constexpr ImU32 kSafeAreaOutlineColor = IM_COL32(255, 200, 0, 200);
        constexpr ImU32 kViewOutlineColor = IM_COL32(80, 160, 255, 220);
        constexpr ImU32 kViewBarTint = IM_COL32(0, 0, 0, 110);
        constexpr ImU32 kCanvasBoundsColor = IM_COL32(190, 90, 255, 200);
        constexpr ImU32 kContentBoundsColor = IM_COL32(255, 130, 220, 200);
        constexpr ImU32 kTouchOkColor = IM_COL32(60, 220, 90, 230);
        constexpr ImU32 kTouchOkFill = IM_COL32(60, 220, 90, 40);
        constexpr ImU32 kTouchSmallColor = IM_COL32(255, 60, 60, 240);
        constexpr ImU32 kTouchSmallFill = IM_COL32(255, 60, 60, 70);
        constexpr ImU32 kRegionColor = IM_COL32(0, 215, 215, 230);
        constexpr ImU32 kRegionFill = IM_COL32(0, 215, 215, 30);
        constexpr float kRegionLabelInset = 3.0f;  // the gap between the outline of a region and its name, in screen units
        // Bars thinner than this (output pixels) are not tinted
        constexpr const char* kPausedMark = ICON_LC_PAUSE " Paused";

        constexpr float kMinBarSize = 0.5f;
        // Two bounds closer than this (output pixels) are the same rect
        constexpr float kSameRectTolerance = 0.5f;
        // The viewport pass runs at density 1: a window unit is an output pixel, what UISettings::minTouchSize is measured in
        constexpr float kViewportPixelDensity = 1.0f;
        constexpr const char* kOverlaysPopup = "##ViewportOverlays";
        constexpr const char* kImageHitArea = "##ViewportImage";
        constexpr const char* kZoomTooltip = "The size of the image on the screen. Wheel: zoom about the pointer, middle button or Space + left button: pan, F: frame the selection (Edit input mode)";
        constexpr const char* kCameraResetTooltip = "Show the whole image again (zoom 100% of the fit, no pan)";

        // In the order of the enums (ViewportAspect, SafeAreaSimulation). The icon names the control (the shape of the image, the
        // safe area), so a combo that shows the current value tells what it is; the labels are literals, nothing is built per frame
        constexpr const char* kAspectLabels[] = {ICON_LC_MAXIMIZE " Free", ICON_LC_PROPORTIONS " 16:9", ICON_LC_PROPORTIONS " 4:3", ICON_LC_PROPORTIONS " 21:9", ICON_LC_PROPORTIONS " 9:16"};
        constexpr const char* kSafeAreaLabels[] = {ICON_LC_SHIELD_CHECK " No safe area", ICON_LC_SHIELD_CHECK " Notch", ICON_LC_SHIELD_CHECK " Side insets", ICON_LC_SHIELD_CHECK " Device"};
        static_assert(std::size(kAspectLabels) == EnumUtils<ViewportAspect>::Count());
        static_assert(std::size(kSafeAreaLabels) == EnumUtils<SafeAreaSimulation>::Count());
        constexpr const char* kCustomLabel = ICON_LC_RULER " Custom...";
        constexpr const char* kViewAutoLabel = ICON_LC_EYE " Auto";
        constexpr const char* kViewDefaultLabel = ICON_LC_EYE " Default";
        // A device at least this wide (logical points, portrait) is shown with the tablet icon
        constexpr uint32_t kTabletMinWidth = 600;
        constexpr const char* kSizeTooltip = "The shape of the image: free (fills the panel), an aspect ratio, a device or a custom size";
        constexpr const char* kRotateTooltip = "Swap the width and the height of a device or custom size, turn the ratio of an aspect (not for Free)";
        constexpr const char* kSafeAreaTooltip = "Simulate a safe area for the canvases: a notch, side insets or the cut-out of the chosen device";
        constexpr const char* kOverlaysTooltip = "What is drawn over the image: the safe area, the bounds of the view and the canvases, the touch targets, the regions";
        constexpr const char* kViewTooltip = "Force a view variant of the scene; Auto picks it by the size of the image";
        constexpr const char* kWidthTooltip = "The width of the image in pixels";
        constexpr const char* kHeightTooltip = "The height of the image in pixels";

        // The icon and the name of a device in the size combo
        String DeviceLabel(const PreviewDevice& device) {
            return Format("{} {} ({}x{})", device.width >= kTabletMinWidth ? ICON_LC_TABLET : ICON_LC_SMARTPHONE, device.name, device.width, device.height);
        }

        // The device of a shape for the Device safe area; none for a custom or an aspect shape and for an index this build lacks
        const PreviewDevice* DeviceOf(const PreviewShape& shape) {
            const auto devices = UIPreview::GetDevices();
            if (shape.custom || shape.device < 0 || static_cast<size_t>(shape.device) >= devices.size()) {
                return nullptr;
            }
            return &devices[static_cast<size_t>(shape.device)];
        }

        // The label of the current shape in the size combo
        String SizeLabel(const PreviewShape& shape) {
            if (shape.custom) {
                return kCustomLabel;
            }
            if (const PreviewDevice* device = DeviceOf(shape)) {
                return DeviceLabel(*device);
            }
            return kAspectLabels[static_cast<size_t>(shape.aspect)];
        }

        // The canvases of a subtree in the order of the scene (the walk of CollectCanvases in UIControl.cpp), without the subtree of a
        // node hidden in the editor: it draws nothing, so it gets no overlay
        void CollectCanvases(const SceneNode& node, eastl::vector<IntrusivePtr<UICanvasComponent>>& out) {
            if (node.HasEditorFlag(EditorFlags::Hidden)) {
                return;
            }
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                out.push_back(std::move(canvas));
            }
            for (const auto& child : node.GetChildren()) {
                CollectCanvases(*child, out);
            }
        }

        // A region drawn this frame, with the rect as the game published it (canvas units) for the tooltip
        struct HoverRegion {
            RegionMark mark;
            Rect canvasRect;
        };

        bool SameRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < kSameRectTolerance && std::abs(a.y - b.y) < kSameRectTolerance && std::abs(a.w - b.w) < kSameRectTolerance && std::abs(a.h - b.h) < kSameRectTolerance;
        }

        bool IsFree(const PreviewShape& shape) {
            return !shape.custom && !DeviceOf(shape) && shape.aspect == ViewportAspect::Free;
        }

    }  // namespace

    ViewportWidget::~ViewportWidget() {
        // Update() clears the overrides right after the scene pass; this is the guarantee that no canvas keeps a simulated safe
        // area or the density of the target in Game mode whatever state the widget dies in
        CoreManager::GetUIEnvironment().SetSafeAreaOverride(std::nullopt);
        CoreManager::GetUIEnvironment().SetPixelDensityOverride(std::nullopt);
        // A gizmo drag in progress gives the nodes their start values back, so the scene is not left half moved
        if (_gizmos.IsDragging()) {
            _gizmos.AbortDrag(CoreManager::GetSceneManager().GetActiveScene());
        }
        // The target's destructor waits for the GPU and gives back its ImGui texture. It needs the device and the ImGui context
        // alive: the widgets die last-first (the viewport before ImGuiWidget) and before the renderer's cleanup
        _renderTarget = {};
    }

    bool ViewportWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorPreferencesChangedEvent, &ViewportWidget::OnPreferencesChanged>(this);
        Subscribe<EditorCommandEvent, &ViewportWidget::OnCommand>(this);
        Subscribe<SceneSelectionChangedEvent, &ViewportWidget::OnSelectionChanged>(this);
        return true;
    }

    void ViewportWidget::OnSelectionChanged(const SceneSelectionChangedEvent& event) {
        _selectedScene = event.scene;
        _selectedNodes.assign(event.nodes.begin(), event.nodes.end());
    }

    void ViewportWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewportInputMode) {
            _inputMode = _inputMode == ViewportInputMode::Play ? ViewportInputMode::Edit : ViewportInputMode::Play;
        } else if (event.commandId == EditorCommandIds::kViewportToolTranslate) {
            _gizmos.SetTool(Gizmo::Tool::Translate);
        } else if (event.commandId == EditorCommandIds::kViewportToolRotate) {
            _gizmos.SetTool(Gizmo::Tool::Rotate);
        } else if (event.commandId == EditorCommandIds::kViewportToolScale) {
            _gizmos.SetTool(Gizmo::Tool::Scale);
        } else if (event.commandId == EditorCommandIds::kViewportGizmoSpace) {
            _gizmos.ToggleSpace();
        } else if (event.commandId == EditorCommandIds::kViewportSnap) {
            _gizmos.ToggleSnap();
        } else if (event.commandId == EditorCommandIds::kViewportFrameSelected) {
            _frameRequested = true;  // the panel size and the base zoom are known in Update
        }
    }

    void ViewportWidget::EmitInputModeIfChanged() {
        if (_inputMode == _storedInputMode) {
            return;
        }
        _storedInputMode = _inputMode;
        ViewportInputModeChangedEvent::Emit(_inputMode);
    }

    void ViewportWidget::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        _gridEnabled = event.viewportGrid;
        _gridStep = event.viewportGridStep;

        // The stored choice; it is also what we last sent, so applying it sends nothing back
        _storedPreview = event.GetPreview();
        _shape = _storedPreview.shape;
        _safeAreaSimulation = _storedPreview.safeArea;
        _overlays = _storedPreview.overlays;

        _storedInputMode = event.viewportInputMode;
        _inputMode = _storedInputMode;
    }

    void ViewportWidget::EmitPreviewIfChanged() {
        if (_editingSize) {
            return;
        }
        const PreviewSettings current{_shape, _safeAreaSimulation, _overlays};
        if (current == _storedPreview) {
            return;
        }
        _storedPreview = current;
        ViewportPreviewChangedEvent::Emit(current);
    }

    void ViewportWidget::Update() {
        BE_PROFILE_SCOPE("ViewportWidget::Update");
        ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport")) {
            ImGui::End();
            return;
        }

        DrawPreviewToolbar();
        ImGui::BeginDisabled(_inputMode != ViewportInputMode::Edit);
        _gizmos.DrawToolbar();
        ImGui::EndDisabled();
        EmitPreviewIfChanged();
        EmitInputModeIfChanged();

        // The target is the shape's size (the largest rect of a preset's ratio that fits what is left of the panel, or the exact size
        // of a device or custom size) and is shown at `zoom`, centered in the panel
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const PreviewTarget target = UIPreview::TargetFor(_shape, available.x, available.y);
        const uint32_t w = target.width;
        const uint32_t h = target.height;
        if (w == 0 || h == 0) {
            ImGui::End();
            return;
        }
        const ImVec2 size{static_cast<float>(w), static_cast<float>(h)};
        const float zoom = target.zoom;
        const ImVec2 shown{size.x * zoom, size.y * zoom};

        IRenderer* renderer = CoreManager::GetRenderer().Get();
        if (!renderer) {
            ImGui::End();
            return;
        }

        // Recreate the render target only when the viewport is resized.
        if (!_renderTarget || _lastWidth != w || _lastHeight != h) {
            _renderTarget = renderer->CreateRenderTarget(w, h);
            _lastWidth = w;
            _lastHeight = h;
            _camera.Reset();  // a camera over another shape of target would look at nothing in particular
        }

        if (_renderTarget) {
            // Set the clear color for the offscreen pass, then begin it.
            renderer->Clear(_clearColor);
            renderer->SetRenderTarget(_renderTarget.Get());
            // The target is not the window, so its safe area is the simulated one (the whole target for "No safe area"), never the window's
            UIEnvironment& uiEnvironment = CoreManager::GetUIEnvironment();
            const Rect safeArea = UIPreview::SimulatedSafeArea(_safeAreaSimulation, DeviceOf(_shape), size.x, size.y);
            uiEnvironment.SetSafeAreaOverride(safeArea);
            // The target has one pixel per ImGui unit at zoom 1, so its pixel density is 1 even when the window runs in the
            // high-density mode, where canvases would otherwise lay out twice as big. A sharper viewport (a target of size times
            // io.DisplayFramebufferScale, the mouse and the safe area scaled with it, the override set to that scale) is a follow-up.
            uiEnvironment.SetPixelDensityOverride(1.0f);
            // The forced view lives for this pass only: Update(), Game mode and the other scenes pick by their own output
            Scene* activeScene = CoreManager::GetSceneManager().GetActiveScene();
            ValidateForcedView(activeScene);
            if (activeScene) {
                activeScene->SetViewOverride(_forcedView);
            }
            CoreManager::GetSceneManager().DrawAll();
            if (activeScene) {
                activeScene->SetViewOverride(std::nullopt);
            }
            uiEnvironment.SetSafeAreaOverride(std::nullopt);
            uiEnvironment.SetPixelDensityOverride(std::nullopt);
            renderer->UnsetRenderTarget();

            // The image is drawn by the draw list at the camera's rect, so the item the layout sees is the hit area over the un-zoomed rect, centered when the shape leaves
            // room on one side. It is an item with an id of its own: the press is always seen (an Image has no id, its hover is fragile) and never starts a window move
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos({cursor.x + (available.x - shown.x) * 0.5f, cursor.y + (available.y - shown.y) * 0.5f});
            const ImVec2 imageMin = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton(kImageHitArea, shown, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            const bool imageHovered = ImGui::IsItemHovered();

            // Where the pointer goes: the camera, the gizmo and the rubber band belong to the Edit input mode, and work while the Edit target owns the pointer (Alt flips it for one click)
            const ImGuiIO& io = ImGui::GetIO();
            const bool editing = _inputMode == ViewportInputMode::Edit;
            const bool editorTarget = ResolveTarget(_inputMode, io.KeyAlt) == ViewportInputTarget::Editor;
            bool panTookPress = false;
            if (editing) {
                panTookPress = _cameraInput.Update(_camera, {imageMin.x, imageMin.y, shown.x, shown.y}, zoom, imageHovered, editorTarget);
                _gizmos.ReadKeys();
                if (ViewportKeyPressed(ImGuiKey_F) || _frameRequested) {
                    FrameSelection(activeScene, shown, zoom);
                }
            } else {
                _cameraInput.Cancel();
            }
            _frameRequested = false;

            const ViewportFrame frame = MakeViewportFrame(editing ? _camera : ViewportCamera{}, imageMin, shown, size, zoom);
            _shownScale = frame.scale;
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
            drawList->AddImage(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), frame.origin, frame.end);
            drawList->PopClipRect();

            DrawGrid(frame);
            DrawOverlays(activeScene, frame, size, safeArea, imageHovered);
            DrawSelectionOutlines(activeScene, frame);

            // The gizmo goes before the mouse is forwarded, so a press on a handle is consumed first
            const bool pointerFree = editing && editorTarget && !_cameraInput.IsPanning();
            const eastl::span<SceneNode* const> selection =
                activeScene && activeScene == _selectedScene ? eastl::span<SceneNode* const>(_selectedNodes.data(), _selectedNodes.size()) : eastl::span<SceneNode* const>();
            const bool gizmoTookPress = _gizmos.Update({activeScene, selection, &frame, editing, pointerFree, imageHovered});
            _boxSelect.Update(frame, activeScene, pointerFree, imageHovered, gizmoTookPress);

            // Hand the mouse to the scene with target-pixel coordinates, so it can map them to scene units (Scene::ScreenToSceneX/Y) with the fit of its last draw
            ForwardMouse(frame, imageHovered, panTookPress || gizmoTookPress);
            if (CoreManager::GetPause().IsPaused()) {
                DrawPausedMark(frame.imageMin, frame.imageMax);
            }
            DrawInputModeMark(frame.imageMin, frame.imageMax, ResolveTarget(_inputMode, io.KeyAlt));
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {}

    void ViewportWidget::DrawSizeCombo() {
        const auto devices = UIPreview::GetDevices();
        const bool onDevice = DeviceOf(_shape) != nullptr;

        // A device brings its own safe area; any other size drops it. The user can pick another simulation after either
        const auto dropDeviceSafeArea = [this] {
            if (_safeAreaSimulation == SafeAreaSimulation::Device) {
                _safeAreaSimulation = SafeAreaSimulation::None;
            }
        };

        ImGui::SetNextItemWidth(kSizeComboWidth);
        if (ImGui::BeginCombo("##ViewportSize", SizePreview())) {
            for (size_t i = 0; i < std::size(kAspectLabels); ++i) {
                const bool selected = !_shape.custom && !onDevice && static_cast<size_t>(_shape.aspect) == i;
                if (ImGui::Selectable(kAspectLabels[i], selected)) {
                    _shape.aspect = static_cast<ViewportAspect>(i);
                    _shape.device = -1;
                    _shape.custom = false;
                    dropDeviceSafeArea();
                }
            }
            ImGui::Separator();
            for (size_t i = 0; i < devices.size(); ++i) {
                const bool selected = !_shape.custom && _shape.device == static_cast<int32_t>(i);
                const PreviewDevice& device = devices[i];
                if (ImGui::Selectable(DeviceLabel(device).c_str(), selected)) {
                    _shape.device = static_cast<int32_t>(i);
                    _shape.custom = false;
                    _safeAreaSimulation = SafeAreaSimulation::Device;
                }
            }
            ImGui::Separator();
            if (ImGui::Selectable(kCustomLabel, _shape.custom)) {
                _shape.custom = true;
                dropDeviceSafeArea();
            }
            ImGui::EndCombo();
        }
        ItemTooltip(kSizeTooltip);
    }

    const char* ViewportWidget::SizePreview() {
        if (!_sizePreviewValid || !(_sizePreviewShape == _shape)) {
            _sizePreview = SizeLabel(_shape);
            _sizePreviewShape = _shape;
            _sizePreviewValid = true;
        }
        return _sizePreview.c_str();
    }

    void ViewportWidget::DrawPreviewToolbar() {
        DrawSizeCombo();

        _editingSize = false;
        if (_shape.custom) {
            // The sides are clamped to the range once the field is left, not while typing ("1" on the way to "1000" is not 64)
            const auto sizeField = [this](const char* label, const char* tooltip, uint32_t& side) {
                const ScopedMonoFont mono;
                int value = static_cast<int>(side);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(kSizeFieldWidth);
                if (ImGui::InputInt(label, &value, 0, 0)) {
                    side = static_cast<uint32_t>(std::max(value, 0));
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    side = std::clamp(side, kMinPreviewSide, kMaxPreviewSide);
                }
                _editingSize = _editingSize || ImGui::IsItemActive();
                ItemTooltip(tooltip);
            };
            sizeField("W", kWidthTooltip, _shape.customWidth);
            sizeField("H", kHeightTooltip, _shape.customHeight);
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(IsFree(_shape));
        ImGui::Checkbox(ICON_LC_ROTATE_CW " Rotate", &_shape.rotated);
        ItemTooltip(kRotateTooltip);
        ImGui::EndDisabled();

        ImGui::SameLine();
        int safeArea = static_cast<int>(_safeAreaSimulation);
        ImGui::SetNextItemWidth(kSafeAreaComboWidth);
        if (ImGui::Combo("##ViewportSafeArea", &safeArea, kSafeAreaLabels, static_cast<int>(std::size(kSafeAreaLabels)))) {
            _safeAreaSimulation = static_cast<SafeAreaSimulation>(safeArea);
        }
        ItemTooltip(kSafeAreaTooltip);

        ImGui::SameLine();
        DrawOverlaysPopup();

        ImGui::SameLine();
        DrawInputModeToggle();

        ImGui::SameLine();
        DrawCameraControls();

        if (const Scene* scene = CoreManager::GetSceneManager().GetActiveScene()) {
            DrawViewCombo(*scene);

            // The view the scene drew with last, picked by the size of this image or forced by the combo (read only)
            const ActiveSceneView view = scene->GetActiveView();
            ImGui::SameLine();
            const StringView name = view.index < 0 ? StringView("default") : view.name.ToStringView();
            ImGui::TextDisabled("View: %.*s %.0f x %.0f", static_cast<int>(name.size()), name.data(), view.width, view.height);
        }
    }

    void ViewportWidget::ValidateForcedView(const Scene* scene) {
        if (!_forcedView) {
            return;
        }
        const bool sameScene = scene && scene->GetName() == _forcedViewScene;
        const bool exists = sameScene && *_forcedView >= -1 && *_forcedView < static_cast<int32_t>(scene->GetViewVariants().size());
        if (!exists) {
            _forcedView = std::nullopt;
        }
    }

    void ViewportWidget::DrawViewCombo(const Scene& scene) {
        const eastl::span<const SceneViewVariant> variants = scene.GetViewVariants();
        if (variants.empty()) {
            return;
        }
        ValidateForcedView(&scene);

        int current = kViewComboAuto;
        if (_forcedView) {
            current = *_forcedView < 0 ? kViewComboDefault : kViewComboFirstVariant + *_forcedView;
        }
        // The text of a variant: its name, or its number when it has none
        const auto variantText = [&variants](size_t index) {
            const SceneViewVariant& variant = variants[index];
            return variant.name.Empty() ? Format("{} #{}", ICON_LC_EYE, index) : Format("{} {}", ICON_LC_EYE, variant.name.ToStringView());
        };

        // The text of the combo is made again only when the forced variant or its name changes
        const char* preview = kViewAutoLabel;
        if (current == kViewComboDefault) {
            preview = kViewDefaultLabel;
        } else if (current >= kViewComboFirstVariant) {
            const size_t index = static_cast<size_t>(*_forcedView);
            const PoolString name = variants[index].name;
            if (_viewPreviewRow != current || _viewPreviewName != name) {
                _viewPreview = variantText(index);
                _viewPreviewRow = current;
                _viewPreviewName = name;
            }
            preview = _viewPreview.c_str();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(kViewComboWidth);
        if (ImGui::BeginCombo("##ViewportView", preview)) {
            if (ImGui::Selectable(kViewAutoLabel, current == kViewComboAuto)) {
                _forcedView = std::nullopt;
            }
            if (ImGui::Selectable(kViewDefaultLabel, current == kViewComboDefault)) {
                _forcedView = -1;
                _forcedViewScene = scene.GetName();
            }
            for (size_t i = 0; i < variants.size(); ++i) {
                const int row = kViewComboFirstVariant + static_cast<int>(i);
                if (ImGui::Selectable(variantText(i).c_str(), current == row)) {
                    _forcedView = static_cast<int32_t>(i);
                    _forcedViewScene = scene.GetName();
                }
            }
            ImGui::EndCombo();
        }
        ItemTooltip(kViewTooltip);
    }

    void ViewportWidget::DrawGrid(const ViewportFrame& frame) const {
        if (!_gridEnabled || _gridStep <= 0) {
            return;
        }
        // A line every _gridStep target pixels from the top-left corner of the target, so it moves and scales with the camera
        const float step = static_cast<float>(_gridStep) * frame.scale;
        if (step < kMinDrawnGridStep) {
            return;
        }
        const float left = std::max(frame.origin.x, frame.imageMin.x);
        const float top = std::max(frame.origin.y, frame.imageMin.y);
        const float right = std::min(frame.end.x, frame.imageMax.x);
        const float bottom = std::min(frame.end.y, frame.imageMax.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        const float firstX = std::max(1.0f, std::ceil((left - frame.origin.x) / step));
        for (float x = frame.origin.x + firstX * step; x < right; x += step) {
            drawList->AddLine({x, top}, {x, bottom}, kGridColor);
        }
        const float firstY = std::max(1.0f, std::ceil((top - frame.origin.y) / step));
        for (float y = frame.origin.y + firstY * step; y < bottom; y += step) {
            drawList->AddLine({left, y}, {right, y}, kGridColor);
        }
        drawList->PopClipRect();
    }

    void ViewportWidget::DrawOverlaysPopup() {
        if (IconTextButton(ICON_LC_LAYERS, "Overlays", kOverlaysTooltip)) {
            ImGui::OpenPopup(kOverlaysPopup);
        }
        if (ImGui::BeginPopup(kOverlaysPopup)) {
            ImGui::Checkbox("Safe area", &_overlays.safeArea);
            ImGui::Checkbox("View bounds", &_overlays.viewBounds);
            ImGui::Checkbox("Canvas bounds", &_overlays.canvasBounds);
            ImGui::Checkbox("Touch targets", &_overlays.touchTargets);
            ImGui::Checkbox("Regions", &_overlays.regions);
            ImGui::EndPopup();
        }
    }

    void ViewportWidget::DrawOverlays(Scene* scene, const ViewportFrame& frame, const ImVec2& outputSize, const Rect& safeArea, bool hovered) const {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);

        // Rects are in target pixels, the frame takes them to the screen
        const auto toScreen = [&](const Rect& rect) { return std::pair<ImVec2, ImVec2>{frame.ToScreen({rect.x, rect.y}), frame.ToScreen({rect.x + rect.w, rect.y + rect.h})}; };
        const auto fill = [&](const Rect& rect, ImU32 color) {
            if (rect.w > kMinBarSize && rect.h > kMinBarSize) {
                const auto [min, max] = toScreen(rect);
                drawList->AddRectFilled(min, max, color);
            }
        };
        const auto outline = [&](const Rect& rect, ImU32 color) {
            const auto [min, max] = toScreen(rect);
            drawList->AddRect(min, max, color);
        };

        if (scene && _overlays.viewBounds) {
            const ActiveSceneView view = scene->GetActiveView();
            const Rect viewRect = UIPreviewOverlay::ViewRect(view.width, view.height, outputSize.x, outputSize.y);
            const float right = viewRect.x + viewRect.w;
            const float bottom = viewRect.y + viewRect.h;
            fill({0.0f, 0.0f, outputSize.x, viewRect.y}, kViewBarTint);
            fill({0.0f, bottom, outputSize.x, outputSize.y - bottom}, kViewBarTint);
            fill({0.0f, viewRect.y, viewRect.x, viewRect.h}, kViewBarTint);
            fill({right, viewRect.y, outputSize.x - right, viewRect.h}, kViewBarTint);
            outline(viewRect, kViewOutlineColor);
        }

        if (scene && (_overlays.canvasBounds || _overlays.touchTargets || _overlays.regions)) {
            eastl::vector<IntrusivePtr<UICanvasComponent>> canvases;
            CollectCanvases(scene->GetRootNode(), canvases);
            const float minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const TouchMark* hoveredMark = nullptr;
            eastl::vector<TouchMark> marks;
            eastl::vector<HoverRegion> regionHits;
            for (const auto& canvas : canvases) {
                const UICanvasLayout& layout = canvas->GetLayout();
                if (!canvas->_visible || !layout.HasLayout()) {
                    continue;
                }
                if (_overlays.canvasBounds) {
                    const Rect bounds = UIPreviewOverlay::CanvasBounds(layout.GetResolved());
                    const Rect content = UIPreviewOverlay::ContentBounds(layout.GetResolved());
                    outline(bounds, kCanvasBoundsColor);
                    if (!SameRect(bounds, content)) {
                        outline(content, kContentBoundsColor);
                    }
                }
                // Regions go under the touch targets
                if (_overlays.regions) {
                    for (RegionMark& mark : UIPreviewOverlay::RegionMarks(layout)) {
                        const auto [min, max] = toScreen(mark.rect);
                        fill(mark.rect, kRegionFill);
                        drawList->AddRect(min, max, kRegionColor);
                        const StringView name = mark.name.ToStringView();
                        const ImVec2 textSize = ImGui::CalcTextSize(name.data(), name.data() + name.size());
                        if (max.x - min.x >= textSize.x + kRegionLabelInset * 2.0f) {
                            drawList->AddText({min.x + kRegionLabelInset, min.y + kRegionLabelInset}, kRegionColor, name.data(), name.data() + name.size());
                        }
                        if (const Rect* canvasRect = layout.regions.Find(mark.name)) {
                            regionHits.push_back({std::move(mark), *canvasRect});
                        }
                    }
                }
                if (_overlays.touchTargets) {
                    for (TouchMark& mark : UIPreviewOverlay::TouchTargets(layout, minTouchSize, kViewportPixelDensity)) {
                        marks.push_back(std::move(mark));
                    }
                }
            }
            // The last mark under the pointer is the one drawn on top
            for (const TouchMark& mark : marks) {
                const auto [min, max] = toScreen(mark.rect);
                drawList->AddRectFilled(min, max, mark.tooSmall ? kTouchSmallFill : kTouchOkFill);
                drawList->AddRect(min, max, mark.tooSmall ? kTouchSmallColor : kTouchOkColor);
                if (hovered && mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y) {
                    hoveredMark = &mark;
                }
            }
            if (hoveredMark) {
                const StringView name = hoveredMark->name.ToStringView();
                ImGui::SetTooltip("%.*s", static_cast<int>(name.size()), name.data());
            } else if (hovered) {
                // The smallest region under the pointer, so a region inside another one stays reachable
                const HoverRegion* smallest = nullptr;
                for (const HoverRegion& hit : regionHits) {
                    const auto [min, max] = toScreen(hit.mark.rect);
                    const bool under = mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y;
                    if (under && (!smallest || hit.mark.rect.w * hit.mark.rect.h < smallest->mark.rect.w * smallest->mark.rect.h)) {
                        smallest = &hit;
                    }
                }
                if (smallest) {
                    const StringView name = smallest->mark.name.ToStringView();
                    const Rect& rect = smallest->canvasRect;
                    ImGui::SetTooltip("%.*s\nx %g  y %g  w %g  h %g", static_cast<int>(name.size()), name.data(), rect.x, rect.y, rect.w, rect.h);
                }
            }
        }

        drawList->PopClipRect();
        DrawSafeAreaOutline(frame, safeArea);
    }

    void ViewportWidget::DrawSelectionOutlines(Scene* scene, const ViewportFrame& frame) const {
        // The pointers are only compared: a deleted node or a replaced scene is never dereferenced
        if (!scene || scene != _selectedScene || _selectedNodes.empty()) {
            return;
        }
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImU32 primaryColor = ImGui::ColorConvertFloat4ToU32(palette.accent);
        const ImU32 otherColor = ImGui::ColorConvertFloat4ToU32(palette.accentMuted);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        for (size_t index = 0; index < _selectedNodes.size(); ++index) {
            const SceneNode* const node = HierarchyModel::FindNode(scene->GetRootNode(), _selectedNodes[index]);
            if (!node) {
                continue;
            }
            const ImU32 color = index + 1 == _selectedNodes.size() ? primaryColor : otherColor;
            const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(*scene, *node);
            for (size_t i = 0; i < outlines.count; ++i) {
                eastl::array<ImVec2, 4> corners;
                for (size_t corner = 0; corner < corners.size(); ++corner) {
                    corners[corner] = frame.ToScreen(outlines.quads[i][corner]);
                }
                drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, kSelectionOutlineThickness);
            }
        }
        drawList->PopClipRect();
    }

    void ViewportWidget::DrawCameraControls() {
        {
            const ScopedMonoFont mono;
            ImGui::TextDisabled("%3.0f%%", _shownScale * kPercent);
        }
        ItemTooltip(kZoomTooltip);
        ImGui::SameLine();
        ImGui::BeginDisabled(_camera.IsIdentity());
        if (IconTextButton(ICON_LC_FOCUS, "Reset##ViewportCamera", kCameraResetTooltip)) {
            _camera.Reset();
        }
        ImGui::EndDisabled();
    }

    void ViewportWidget::FrameSelection(Scene* scene, const ImVec2& panelSize, float baseZoom) {
        Rect bounds;
        if (scene && scene == _selectedScene && SelectionBounds(*scene, {_selectedNodes.data(), _selectedNodes.size()}, bounds)) {
            _camera.FrameRect(bounds, {panelSize.x, panelSize.y}, baseZoom, kFrameMargin);
        } else {
            _camera.Reset();
        }
    }

    void ViewportWidget::DrawPausedMark(const ImVec2& imageMin, const ImVec2& imageMax) const {
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(kPausedMark);
        const ImVec2 pad = style.FramePadding;
        const ImVec2 min(imageMin.x + style.WindowPadding.x, imageMin.y + style.WindowPadding.y);
        const ImVec2 max(min.x + textSize.x + pad.x * 2.0f, min.y + textSize.y + pad.y * 2.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(imageMin, imageMax, true);
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(palette.accentMuted), style.FrameRounding);
        drawList->AddText(ImVec2(min.x + pad.x, min.y + pad.y), ImGui::ColorConvertFloat4ToU32(palette.text), kPausedMark);
        drawList->PopClipRect();
    }

    void ViewportWidget::DrawSafeAreaOutline(const ViewportFrame& frame, const Rect& safeArea) const {
        if (!_overlays.safeArea || _safeAreaSimulation == SafeAreaSimulation::None) {
            return;
        }
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        drawList->AddRect(frame.ToScreen({safeArea.x, safeArea.y}), frame.ToScreen({safeArea.x + safeArea.w, safeArea.y + safeArea.h}), kSafeAreaOutlineColor);
        drawList->PopClipRect();
    }

    void ViewportWidget::DrawInputModeToggle() {
        const bool play = _inputMode == ViewportInputMode::Play;
        if (IconTextButton(play ? ICON_LC_GAMEPAD_2 : ICON_LC_MOUSE_POINTER_2, play ? "Play##ViewportInputMode" : "Edit##ViewportInputMode", kInputModeTooltip)) {
            _inputMode = play ? ViewportInputMode::Edit : ViewportInputMode::Play;
        }
    }

    void ViewportWidget::DrawInputModeMark(const ImVec2& imageMin, const ImVec2& imageMax, ViewportInputTarget target) const {
        const bool edit = target == ViewportInputTarget::Editor;
        const char* mark = edit ? kEditMark : kPlayMark;
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(mark);
        const ImVec2 pad = style.FramePadding;
        const float width = textSize.x + pad.x * 2.0f;
        const ImVec2 min(imageMax.x - style.WindowPadding.x - width, imageMin.y + style.WindowPadding.y);
        const ImVec2 max(min.x + width, min.y + textSize.y + pad.y * 2.0f);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(imageMin, imageMax, true);
        if (edit) {
            // Inside the image: the outline is half of its thickness in from the edge
            const float inset = kEditOutlineThickness * 0.5f;
            drawList->AddRect({imageMin.x + inset, imageMin.y + inset}, {imageMax.x - inset, imageMax.y - inset}, ImGui::ColorConvertFloat4ToU32(palette.accent), 0.0f, 0, kEditOutlineThickness);
        }
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(palette.accentMuted), style.FrameRounding);
        drawList->AddText(ImVec2(min.x + pad.x, min.y + pad.y), ImGui::ColorConvertFloat4ToU32(palette.text), mark);
        drawList->PopClipRect();
    }

    void ViewportWidget::ForwardMouse(const ViewportFrame& frame, bool hovered, bool pressTaken) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool modifier = io.KeyAlt;

        // A paused game gets no mouse: nothing is sent or kept, and a press that began before the pause is forgotten, so the
        // first event after the resume is not a stray button-up. An edit click is the editor's, so it still goes through
        if (CoreManager::GetPause().IsPaused()) {
            _pointer.Reset();
            if (ResolveTarget(_inputMode, modifier) == ViewportInputTarget::Game) {
                return;
            }
        }

        // The scene lives in target pixels, the image on screen is `scale` times that
        ViewportPointerFrame pointer;
        const Vec2 position = frame.ToTarget(io.MousePos);
        pointer.x = position.x;
        pointer.y = position.y;
        pointer.deltaX = io.MouseDelta.x / frame.scale;
        pointer.deltaY = io.MouseDelta.y / frame.scale;
        pointer.hovered = hovered;
        for (int i = 0; i < kViewportButtonCount; ++i) {
            pointer.down[i] = io.MouseDown[i];
            pointer.clicked[i] = ImGui::IsMouseClicked(i);
            pointer.released[i] = ImGui::IsMouseReleased(i);
            pointer.clicks[i] = static_cast<uint8_t>(io.MouseClickedCount[i]);
        }
        pointer.wheelX = io.MouseWheelH;
        pointer.wheelY = io.MouseWheel;
        pointer.modifiers = SelectionModifiersOf(io.KeyCtrl, io.KeyShift);
        switch (io.MouseSource) {
            case ImGuiMouseSource_TouchScreen:
                pointer.source = ViewportPointerSource::Touch;
                break;
            case ImGuiMouseSource_Pen:
                pointer.source = ViewportPointerSource::Pen;
                break;
            default:
                break;
        }

        // A press that a pan or a gizmo took gets an empty sink: the press is still followed to its release, but no node is picked under the handle
        ViewportPointer::EditClickSink sink;
        if (!pressTaken) {
            sink = [](const ViewportEditClickEvent& click) { ViewportEditClickEvent::Emit(click.x, click.y, click.button, click.modifiers); };
        }
        _pointer.Forward(pointer, _inputMode, modifier, CoreManager::GetUIInput(), sink);
    }

}  // namespace BECore
