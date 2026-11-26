#include "ViewportWidget.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Time/GamePause.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <Generated/ViewportEvents.gen.hpp>
#include <Generated/ViewportWidget.gen.hpp>
#include <Widgets/AssetBrowser/AssetDrag.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/SceneAssetDrop.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <Widgets/Viewport/ViewportUIDebug.h>
#include <Widgets/Viewport/ViewportUIGizmos.h>
#include <imgui.h>

namespace BECore {

    namespace {

        constexpr float kEditOutlineThickness = 2.0f;       // the accent outline of the image in the Edit input mode, in screen units
        constexpr float kSelectionOutlineThickness = 2.0f;  // the outline of the selected node, in screen units
        constexpr float kPercent = 100.0f;
        constexpr float kFrameMargin = 24.0f;  // the room around the selection that F leaves, in screen units
        constexpr const char* kPlayMark = ICON_LC_GAMEPAD_2 " Play";
        constexpr const char* kEditMark = ICON_LC_MOUSE_POINTER_2 " Edit";
        // The first toolbar row of the window, the asset of ViewportToolbarModel
        constexpr StringView kToolbarAsset = "ViewportToolbarPanel";
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
        constexpr const char* kImageHitArea = "##ViewportImage";

        // The device of a shape for the Device safe area; none for a custom or an aspect shape and for an index this build lacks
        const PreviewDevice* DeviceOf(const PreviewShape& shape) {
            const auto devices = UIPreview::GetDevices();
            if (shape.custom || shape.device < 0 || static_cast<size_t>(shape.device) >= devices.size()) {
                return nullptr;
            }
            return &devices[static_cast<size_t>(shape.device)];
        }

        bool SameRect(const Rect& a, const Rect& b) {
            return std::abs(a.x - b.x) < kSameRectTolerance && std::abs(a.y - b.y) < kSameRectTolerance && std::abs(a.w - b.w) < kSameRectTolerance && std::abs(a.h - b.h) < kSameRectTolerance;
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
        _uiGizmos.AbortDrag(CoreManager::GetSceneManager().GetActiveScene());
        // The target's destructor waits for the GPU and gives back its ImGui texture. It needs the device and the ImGui context
        // alive: the widgets die last-first (the viewport before ImGuiWidget) and before the renderer's cleanup
        _renderTarget = {};
    }

    bool ViewportWidget::Initialize(IDeserializer& /*deserializer*/) {
        Subscribe<EditorPreferencesChangedEvent, &ViewportWidget::OnPreferencesChanged>(this);
        Subscribe<EditorCommandEvent, &ViewportWidget::OnCommand>(this);
        Subscribe<SceneSelectionChangedEvent, &ViewportWidget::OnSelectionChanged>(this);
        Subscribe<UIElementSelectedEvent, &ViewportWidget::OnUIElementSelected>(this);
        return true;
    }

    void ViewportWidget::OnUIElementSelected(const UIElementSelectedEvent& event) {
        _uiGizmos.OnSelected(event);
    }

    void ViewportWidget::OnSelectionChanged(const SceneSelectionChangedEvent& event) {
        _selectedScene = event.scene;
        _selectedNodes.assign(event.nodes.begin(), event.nodes.end());
    }

    void ViewportWidget::OnCommand(const EditorCommandEvent& event) {
        if (event.commandId == EditorCommandIds::kViewViewport) {
            _open = !_open;
        } else if (event.commandId == EditorCommandIds::kViewportFrameSelected) {
            _frameRequested = true;  // the panel size and the base zoom are known in Update
        } else if (event.commandId == EditorCommandIds::kViewportCameraReset) {
            _camera.Reset();
        }
    }

    void ViewportWidget::OnPreferencesChanged(const EditorPreferencesChangedEvent& event) {
        _gridEnabled = event.viewportGrid;
        _gridStep = event.viewportGridStep;
    }

    void ViewportWidget::Update() {
        BE_PROFILE_SCOPE("ViewportWidget::Update");
        if (!_open) {
            return;
        }
        DrawViewportWindow();
        // Its own window, so it can be docked beside the viewport; drawn from the findings the viewport's pass just made
        IViewportToolbar& toolbar = ViewportToolbarRegistry::GetInstance().GetOrDefault();
        if (toolbar.GetPreview().overlays.findings && !_uiDebug.DrawFindingsWindow()) {
            toolbar.ClearFindingsOverlay();
        }
    }

    void ViewportWidget::DrawViewportWindow() {
        ImGui::SetNextWindowSize({800.0f, 600.0f}, ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Viewport", &_open)) {
            ImGui::End();
            return;
        }

        // The choices are read after the bar is drawn: its model takes what the controls wrote at the end of the draw, so a choice applies in this frame
        PanelBarsRegistry::Draw(kToolbarAsset);
        IViewportToolbar& toolbar = ViewportToolbarRegistry::GetInstance().GetOrDefault();
        const PreviewSettings preview = toolbar.GetPreview();
        const ViewportInputMode inputMode = toolbar.GetInputMode();

        // The target is the shape's size (the largest rect of a preset's ratio that fits what is left of the panel, or the exact size
        // of a device or custom size) and is shown at `zoom`, centered in the panel
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const PreviewTarget target = UIPreview::TargetFor(preview.shape, available.x, available.y);
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

        if (!_renderTarget || _lastWidth != w || _lastHeight != h) {
            _renderTarget = renderer->CreateRenderTarget(w, h);
            _lastWidth = w;
            _lastHeight = h;
            _camera.Reset();  // a camera over another shape of target would look at nothing in particular
        }

        if (_renderTarget) {
            renderer->Clear(_clearColor);
            renderer->SetRenderTarget(_renderTarget.Get());
            // The target is not the window, so its safe area is the simulated one (the whole target for "No safe area"), never the window's
            UIEnvironment& uiEnvironment = CoreManager::GetUIEnvironment();
            const Rect safeArea = UIPreview::SimulatedSafeArea(preview.safeArea, DeviceOf(preview.shape), size.x, size.y);
            uiEnvironment.SetSafeAreaOverride(safeArea);
            // The target has one pixel per ImGui unit at zoom 1, so its pixel density is 1 even when the window runs in the
            // high-density mode, where canvases would otherwise lay out twice as big
            uiEnvironment.SetPixelDensityOverride(1.0f);
            // The forced view lives for this pass only: Update(), Game mode and the other scenes pick by their own output
            Scene* activeScene = CoreManager::GetSceneManager().GetActiveScene();
            if (activeScene) {
                activeScene->SetViewOverride(toolbar.GetForcedView(activeScene));
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
            // Taken while the hit area is the last item; sent once the frame of this pass is known
            const Optional<AssetDragPayload> droppedAsset = AcceptAssetDrop(MakesSceneNode);

            // One walk of the tree confirms the whole selection; the outlines, the framing and the node gizmo take this list and never look a node up themselves
            if (activeScene && activeScene == _selectedScene) {
                HierarchyModel::ResolveLive(activeScene->GetRootNode(), {_selectedNodes.data(), _selectedNodes.size()}, _liveSelection, _liveScratch);
            } else {
                _liveSelection.clear();
            }

            // Where the pointer goes: the camera, the gizmo and the rubber band belong to the Edit input mode, and work while the Edit target owns the pointer (Alt flips it for one click)
            const ImGuiIO& io = ImGui::GetIO();
            const bool editing = inputMode == ViewportInputMode::Edit;
            const bool editorTarget = ResolveTarget(inputMode, io.KeyAlt) == ViewportInputTarget::Editor;
            bool panTookPress = false;
            if (editing) {
                panTookPress = _cameraInput.Update(_camera, {imageMin.x, imageMin.y, shown.x, shown.y}, zoom, imageHovered, editorTarget);
                _gizmos.ReadKeys(toolbar);
                if (ViewportKeyPressed(ImGuiKey_F) || _frameRequested) {
                    FrameSelection(activeScene, shown, zoom);
                }
            } else {
                _cameraInput.Cancel();
            }
            _frameRequested = false;

            const ViewportFrame frame = MakeViewportFrame(editing ? _camera : ViewportCamera{}, imageMin, shown, size, zoom);
            toolbar.PublishCamera(frame.scale * kPercent, !_camera.IsIdentity());
            ImDrawList* const drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
            drawList->AddImage(reinterpret_cast<ImTextureID>(_renderTarget->GetImGuiTextureId()), frame.origin, frame.end);
            drawList->PopClipRect();

            DrawGrid(frame);
            DrawOverlays(activeScene, frame, size, preview.overlays, toolbar, imageHovered);
            if (preview.overlays.safeArea && preview.safeArea != SafeAreaSimulation::None) {
                DrawSafeAreaOutline(frame, safeArea);
            }
            DrawSelectionOutlines(activeScene, frame);

            if (droppedAsset) {
                const Vec2 at = frame.ToTarget(ImGui::GetIO().MousePos);
                ViewportAssetDropEvent::Emit(droppedAsset->kind, PoolString::Intern(StringView(droppedAsset->path)), at.x, at.y);
            }

            // The gizmos go before the mouse is forwarded, so a press on a handle is consumed first. The UI element's handles come before the node gizmo, which stays out of a drag of
            // theirs and of a pointer over one of them
            const bool pointerFree = editing && editorTarget && !_cameraInput.IsPanning();
            const SceneNode* const primary = activeScene && activeScene == _selectedScene && !_selectedNodes.empty() ? _selectedNodes.back() : nullptr;
            const bool uiTookPress = _uiGizmos.Update({activeScene, primary, &frame, editing, pointerFree && !_gizmos.IsDragging(), imageHovered});
            const bool nodeGizmoFree = pointerFree && !_uiGizmos.IsDragging() && !_uiGizmos.IsOverHandle();
            const bool gizmoTookPress =
                _gizmos.Update({activeScene, {_liveSelection.data(), _liveSelection.size()}, &frame, editing, nodeGizmoFree, imageHovered, toolbar.GetGizmoSettings()}) || uiTookPress;
            _boxSelect.Update(frame, activeScene, pointerFree, imageHovered, gizmoTookPress);

            // Hand the mouse to the scene with target-pixel coordinates, so it can map them to scene units (Scene::ScreenToSceneX/Y) with the fit of its last draw
            ForwardMouse(frame, inputMode, imageHovered, panTookPress || gizmoTookPress);
            if (CoreManager::GetPause().IsPaused()) {
                DrawPausedMark(frame.imageMin, frame.imageMax);
            }
            DrawInputModeMark(frame.imageMin, frame.imageMax, ResolveTarget(inputMode, io.KeyAlt));
        }

        ImGui::End();
    }

    void ViewportWidget::Draw() {}

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

    void ViewportWidget::DrawOverlays(Scene* scene, const ViewportFrame& frame, const ImVec2& outputSize, const PreviewOverlays& overlays, IViewportToolbar& toolbar, bool hovered) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);

        // Rects are in target pixels, the frame takes them to the screen
        const auto toScreen = [&](const Rect& rect) { return Pair<ImVec2, ImVec2>{frame.ToScreen({rect.x, rect.y}), frame.ToScreen({rect.x + rect.w, rect.y + rect.h})}; };
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

        if (scene && overlays.viewBounds) {
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

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const TouchMark* hoveredMark = nullptr;
        // The vectors are members and keep their capacity, so a steady frame allocates nothing; the pointer above points into _touchMarks until the next frame clears it
        _touchMarks.clear();
        _regionHits.clear();
        if (scene && (overlays.canvasBounds || overlays.touchTargets || overlays.regions)) {
            _overlayCanvases.clear();
            ViewportUIDebug::CollectCanvases(scene->GetRootNode(), _overlayCanvases);
            const float minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            for (const auto& canvas : _overlayCanvases) {
                const UICanvasLayout& layout = canvas->GetLayout();
                if (!canvas->_visible || !layout.HasLayout()) {
                    continue;
                }
                if (overlays.canvasBounds) {
                    const Rect bounds = UIPreviewOverlay::CanvasBounds(layout.GetResolved());
                    const Rect content = UIPreviewOverlay::ContentBounds(layout.GetResolved());
                    outline(bounds, kCanvasBoundsColor);
                    if (!SameRect(bounds, content)) {
                        outline(content, kContentBoundsColor);
                    }
                }
                // Regions go under the touch targets
                if (overlays.regions) {
                    UIPreviewOverlay::RegionMarks(layout, _canvasRegionMarks);
                    for (const RegionMark& mark : _canvasRegionMarks) {
                        const auto [min, max] = toScreen(mark.rect);
                        fill(mark.rect, kRegionFill);
                        drawList->AddRect(min, max, kRegionColor);
                        const StringView name = mark.name.ToStringView();
                        const ImVec2 textSize = ImGui::CalcTextSize(name.data(), name.data() + name.size());
                        if (max.x - min.x >= textSize.x + kRegionLabelInset * 2.0f) {
                            drawList->AddText({min.x + kRegionLabelInset, min.y + kRegionLabelInset}, kRegionColor, name.data(), name.data() + name.size());
                        }
                        if (const Rect* canvasRect = layout.regions.Find(mark.name)) {
                            _regionHits.push_back({mark, *canvasRect});
                        }
                    }
                }
                if (overlays.touchTargets) {
                    UIPreviewOverlay::TouchTargets(layout, minTouchSize, kViewportPixelDensity, _canvasTouchMarks);
                    _touchMarks.insert(_touchMarks.end(), _canvasTouchMarks.begin(), _canvasTouchMarks.end());
                }
            }
            // The pointers are not kept past the frame: a canvas must not outlive its scene through the widget
            _overlayCanvases.clear();
            // The last mark under the pointer is the one drawn on top
            for (const TouchMark& mark : _touchMarks) {
                const auto [min, max] = toScreen(mark.rect);
                drawList->AddRectFilled(min, max, mark.tooSmall ? kTouchSmallFill : kTouchOkFill);
                drawList->AddRect(min, max, mark.tooSmall ? kTouchSmallColor : kTouchOkColor);
                if (hovered && mouse.x >= min.x && mouse.x < max.x && mouse.y >= min.y && mouse.y < max.y) {
                    hoveredMark = &mark;
                }
            }
        }

        // The UI debug overlays go over the touch targets; nothing is collected or validated while all four are off
        const bool uiDebug = scene && ViewportUIDebug::AnyOn(overlays);
        if (uiDebug) {
            _uiDebug.Collect(scene, [&toolbar](const SceneNode* node) { return toolbar.IsCanvasEnabled(node); });
            _uiDebug.Draw(*drawList, frame, overlays, hovered, mouse);
        } else {
            _uiDebug.Release();
        }
        // An empty list while the four are off: the Overlays popup lists the canvases only while one of them is on
        toolbar.PublishCanvases(scene, _uiDebug.GetCanvasRows());

        // The selected element in the shapes checked in the popup, nothing is probed while none is
        _uiShapes.SetShapes(toolbar.GetShapeMask(), toolbar.IsLandscapeShapes());
        const UISelectedElement selected = scene && _uiShapes.AnyChecked() ? _uiGizmos.FindSelected(scene) : UISelectedElement{};
        if (selected && selected.canvas->_visible) {
            _uiShapes.Draw(*drawList, frame, {outputSize.x, outputSize.y}, selected.canvas->GetLayout(), *selected.element);
        } else {
            _uiShapes.Release();
        }

        // One tooltip per frame: a finding, a touch target, the element under the pointer, an outline of another shape, a region
        bool tooltipShown = uiDebug && _uiDebug.ShowFindingTooltip();
        if (!tooltipShown && hoveredMark) {
            const StringView name = hoveredMark->name.ToStringView();
            ImGui::SetTooltip("%.*s", static_cast<int>(name.size()), name.data());
            tooltipShown = true;
        }
        if (!tooltipShown && uiDebug) {
            tooltipShown = _uiDebug.ShowElementTooltip();
        }
        if (!tooltipShown && hovered) {
            tooltipShown = _uiShapes.ShowTooltip(mouse);
        }
        if (!tooltipShown && hovered) {
            // The smallest region under the pointer, so a region inside another one stays reachable
            const HoverRegion* smallest = nullptr;
            for (const HoverRegion& hit : _regionHits) {
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

        drawList->PopClipRect();
    }

    void ViewportWidget::DrawSelectionOutlines(Scene* scene, const ViewportFrame& frame) const {
        // _liveSelection is this frame's confirmation of the selection: a deleted node or a replaced scene is never dereferenced
        if (!scene || scene != _selectedScene || _liveSelection.empty()) {
            return;
        }
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImU32 primaryColor = ImGui::ColorConvertFloat4ToU32(palette.accent);
        const ImU32 otherColor = ImGui::ColorConvertFloat4ToU32(palette.accentMuted);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        const SceneNode* const primary = _selectedNodes.back();
        for (const SceneNode* const node : _liveSelection) {
            const ImU32 color = node == primary ? primaryColor : otherColor;
            const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(*scene, *node);
            for (size_t i = 0; i < outlines.count; ++i) {
                Array<ImVec2, 4> corners;
                for (size_t corner = 0; corner < corners.size(); ++corner) {
                    corners[corner] = frame.ToScreen(outlines.quads[i][corner]);
                }
                drawList->AddPolyline(corners.data(), static_cast<int>(corners.size()), color, ImDrawFlags_Closed, kSelectionOutlineThickness);
            }
        }
        drawList->PopClipRect();
    }

    void ViewportWidget::FrameSelection(Scene* scene, const ImVec2& panelSize, float baseZoom) {
        Rect bounds;
        if (scene && scene == _selectedScene && SelectionBounds(*scene, {_liveSelection.data(), _liveSelection.size()}, bounds)) {
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
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        drawList->AddRect(frame.ToScreen({safeArea.x, safeArea.y}), frame.ToScreen({safeArea.x + safeArea.w, safeArea.y + safeArea.h}), kSafeAreaOutlineColor);
        drawList->PopClipRect();
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

    void ViewportWidget::ForwardMouse(const ViewportFrame& frame, ViewportInputMode inputMode, bool hovered, bool pressTaken) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool modifier = io.KeyAlt;

        // A paused game gets no mouse: nothing is sent or kept, and a press that began before the pause is forgotten, so the
        // first event after the resume is not a stray button-up. An edit click is the editor's, so it still goes through
        if (CoreManager::GetPause().IsPaused()) {
            _pointer.Reset();
            if (ResolveTarget(inputMode, modifier) == ViewportInputTarget::Game) {
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
        _pointer.Forward(pointer, inputMode, modifier, CoreManager::GetUIInput(), sink);
    }

}  // namespace BECore
