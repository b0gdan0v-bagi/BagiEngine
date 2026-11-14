#include "ViewportGizmos.h"

#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <EASTL/algorithm.h>
#include <Widgets/EditorLayout/EditorControls.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/ScenePick.h>
#include <Widgets/SceneEditor/SceneSelection.h>
#include <Widgets/Viewport/ViewportInput.h>

namespace BECore {

    namespace {

        constexpr int kLeftButton = 0;
        constexpr int kMiddleButton = 2;

        // The gizmo, in screen units (the geometry's sizes are in screen pixels too: GizmoGeometry.h)
        constexpr float kGizmoLineThickness = 2.0f;
        constexpr float kArrowHeadLength = 12.0f;
        constexpr float kArrowHeadHalfWidth = 5.0f;
        constexpr float kHandleDrawHalfSize = 4.5f;
        constexpr ImU32 kAxisXColor = IM_COL32(235, 80, 80, 255);
        constexpr ImU32 kAxisYColor = IM_COL32(90, 205, 100, 255);
        constexpr ImU32 kFreeColor = IM_COL32(235, 235, 235, 200);
        constexpr ImU32 kRingColor = IM_COL32(90, 170, 255, 255);
        constexpr ImU32 kScaleColor = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kScaleBoxColor = IM_COL32(255, 255, 255, 160);
        constexpr float kScaleBoxThickness = 1.0f;
        constexpr ImU32 kHandleOutlineColor = IM_COL32(0, 0, 0, 200);
        constexpr ImU32 kHighlightColor = IM_COL32(255, 220, 70, 255);
        constexpr float kRubberBandFillAlpha = 0.18f;
        constexpr float kRubberBandThickness = 1.0f;

        // The range of the steps in the Snap popup
        constexpr float kMinSnapStep = 0.01f;
        constexpr float kMaxPositionStep = 1024.0f;
        constexpr float kMaxAngleStep = 180.0f;
        constexpr float kMaxScaleStep = 1.0f;
        constexpr float kPositionStepSpeed = 0.5f;
        constexpr float kAngleStepSpeed = 0.5f;
        constexpr float kScaleStepSpeed = 0.01f;
        constexpr float kSnapStepWidth = 70.0f;

        constexpr const char* kSnapPopup = "##ViewportSnap";
        constexpr const char* kMoveTooltip = "Move the selected nodes: drag an arrow or the square (W)";
        constexpr const char* kRotateTooltip = "Rotate the selected nodes about the pivot: drag the ring (E)";
        constexpr const char* kScaleTooltip = "Scale the selected nodes: drag a corner or a side; Shift keeps the aspect (R)";
        constexpr const char* kSpaceTooltip = "The axes of the Move gizmo: World (the screen's) or Local (the turned axes of the node); a group always uses World (X)";
        constexpr const char* kSnapTooltip = "Snap a drag to a step; hold Ctrl (Cmd) to flip the switches for one drag";

        ImVec2 ToImVec(Vec2 point) {
            return {point.x, point.y};
        }

        ImVec2 Along(const ImVec2& origin, Vec2 direction, float length) {
            return {origin.x + direction.x * length, origin.y + direction.y * length};
        }

        /// The scene point under the mouse
        Vec2 ScenePointOf(const ViewportGizmoContext& context, const ImVec2& mouse) {
            const Vec2 output = context.frame->ToTarget(mouse);
            return {context.scene->ScreenToSceneX(output.x), context.scene->ScreenToSceneY(output.y)};
        }

        /// How many scene units one screen unit is, through the scale of the view and of the frame
        float UnitsPerScreenPixel(const ViewportGizmoContext& context) {
            const float screenPerUnit = context.frame->scale * context.scene->GetViewTransform().scale;
            return screenPerUnit > 0.0f ? 1.0f / screenPerUnit : 1.0f;
        }

        /// A scene point on the screen: the view of the scene takes it to the render output, the frame to the screen
        ImVec2 SceneToScreen(const ViewportFrame& frame, const ViewTransform& view, Vec2 point) {
            const Rect output = view.ToOutput(point.x, point.y, 0.0f, 0.0f);
            return frame.ToScreen({output.x, output.y});
        }

        void DrawArrow(ImDrawList& drawList, const ImVec2& origin, Vec2 direction, ImU32 color) {
            const Vec2 normal{-direction.y, direction.x};
            const ImVec2 tip = Along(origin, direction, Gizmo::kArrowLength);
            const ImVec2 neck = Along(origin, direction, Gizmo::kArrowLength - kArrowHeadLength);
            drawList.AddLine(origin, neck, color, kGizmoLineThickness);
            drawList.AddTriangleFilled(tip, Along(neck, normal, kArrowHeadHalfWidth), Along(neck, normal, -kArrowHeadHalfWidth), color);
        }

    }  // namespace

    ViewportFrame MakeViewportFrame(const ViewportCamera& camera, const ImVec2& panelMin, const ImVec2& panelSize, const ImVec2& targetSize, float baseZoom) {
        const Rect panel{panelMin.x, panelMin.y, panelSize.x, panelSize.y};
        const Vec2 origin = camera.ToScreen({0.0f, 0.0f}, panel, baseZoom);
        ViewportFrame frame;
        frame.imageMin = panelMin;
        frame.imageMax = {panelMin.x + panelSize.x, panelMin.y + panelSize.y};
        frame.scale = camera.Scale(baseZoom);
        frame.origin = ToImVec(origin);
        frame.end = {origin.x + targetSize.x * frame.scale, origin.y + targetSize.y * frame.scale};
        return frame;
    }

    Gizmo::Item ItemOf(const TransformComponent& transform) {
        return {transform._x, transform._y, transform._width, transform._height, transform._rotation, transform._pivotX, transform._pivotY};
    }

    void ApplyItem(TransformComponent& transform, const Gizmo::Item& item) {
        transform._x = item.x;
        transform._y = item.y;
        transform._width = item.width;
        transform._height = item.height;
        transform._rotation = item.rotation;
        transform._pivotX = item.pivotX;
        transform._pivotY = item.pivotY;
    }

    bool IsMovable(const SceneNode& node) {
        return !node.HasEditorFlag(EditorFlags::Locked) && node.GetComponent<TransformComponent>();
    }

    void CollectMovable(Scene& scene, Span<SceneNode* const> selected, Vector<SceneNode*>& nodes, Vector<Gizmo::Item>& items) {
        nodes.clear();
        items.clear();
        for (SceneNode* const node : selected) {
            SceneNode* const alive = HierarchyModel::FindNode(scene.GetRootNode(), node);
            if (alive && IsMovable(*alive)) {
                nodes.push_back(alive);
                items.push_back(ItemOf(*alive->GetComponent<TransformComponent>()));
            }
        }
    }

    bool AllAlive(Scene& scene, Span<SceneNode* const> nodes) {
        return eastl::all_of(nodes.begin(), nodes.end(), [&scene](SceneNode* node) { return HierarchyModel::FindNode(scene.GetRootNode(), node) != nullptr; });
    }

    bool WriteItems(Scene& scene, Span<SceneNode* const> nodes, Span<const Gizmo::Item> items) {
        if (nodes.size() != items.size()) {
            return false;
        }
        const bool writable = eastl::all_of(nodes.begin(), nodes.end(), [&scene](SceneNode* node) {
            const SceneNode* const alive = HierarchyModel::FindNode(scene.GetRootNode(), node);
            return alive && alive->GetComponent<TransformComponent>();
        });
        if (!writable) {
            return false;
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            ApplyItem(*nodes[i]->GetComponent<TransformComponent>(), items[i]);
        }
        return true;
    }

    bool SelectionBounds(Scene& scene, Span<SceneNode* const> selected, Rect& bounds) {
        Vec2 low{std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        Vec2 high{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        bool any = false;
        for (SceneNode* const node : selected) {
            const SceneNode* const alive = HierarchyModel::FindNode(scene.GetRootNode(), node);
            if (!alive) {
                continue;
            }
            const ScenePick::Outlines outlines = ScenePick::SelectionOutlines(scene, *alive);
            for (size_t i = 0; i < outlines.count; ++i) {
                for (const Vec2& corner : outlines.quads[i]) {
                    low = {std::min(low.x, corner.x), std::min(low.y, corner.y)};
                    high = {std::max(high.x, corner.x), std::max(high.y, corner.y)};
                    any = true;
                }
            }
        }
        if (any) {
            bounds = {low.x, low.y, high.x - low.x, high.y - low.y};
        }
        return any;
    }

    bool ViewportKeyPressed(ImGuiKey key) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeyShift || io.KeySuper) {
            return false;
        }
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
            return false;
        }
        return ImGui::IsKeyPressed(key, false);
    }

    bool ViewportCameraInput::Update(ViewportCamera& camera, const Rect& panel, float baseZoom, bool hovered, bool enabled) {
        const ImGuiIO& io = ImGui::GetIO();
        if (_panning && (!enabled || !io.MouseDown[_button])) {
            _panning = false;
        }
        if (_panning) {
            camera.PanBy({io.MouseDelta.x, io.MouseDelta.y}, baseZoom);
            return false;
        }
        if (!enabled || !hovered) {
            return false;
        }
        if (ImGui::IsMouseClicked(kMiddleButton)) {
            _panning = true;
            _button = kMiddleButton;
            return true;
        }
        if (ImGui::IsMouseClicked(kLeftButton) && ImGui::IsKeyDown(ImGuiKey_Space)) {
            _panning = true;
            _button = kLeftButton;
            return true;
        }
        if (io.MouseWheel != 0.0f) {
            camera.ZoomAt(ViewportCamera::WheelFactor(io.MouseWheel), {io.MousePos.x, io.MousePos.y}, panel, baseZoom);
        }
        return false;
    }

    bool GizmoEditSession::Begin(Scene& scene, Span<SceneNode* const> nodes, Span<const Gizmo::Item> items, Gizmo::Tool tool, Gizmo::Space space, Gizmo::Handle handle, Vec2 point,
                                 const Gizmo::SnapSettings& snap) {
        if (nodes.size() != items.size()) {
            return false;
        }
        _drag.Begin(tool, space, handle, items, point, snap);
        if (!_drag.IsActive()) {
            return false;
        }
        _scene = &scene;
        _handle = handle;
        _nodes.assign(nodes.begin(), nodes.end());
        _edits.clear();
        for (size_t i = 0; i < nodes.size(); ++i) {
            _edits.push_back({nodes[i], items[i], items[i]});
        }
        Emit(NodeTransformEditPhase::Begin);
        return true;
    }

    bool GizmoEditSession::CanContinue(Scene* activeScene) const {
        // A node that lost its TransformComponent has nothing to write to, so the drag ends as it does for a dead node
        return activeScene != nullptr && activeScene == _scene && AllAlive(*activeScene, {_nodes.data(), _nodes.size()}) &&
               eastl::all_of(_nodes.begin(), _nodes.end(), [](const SceneNode* node) { return node->GetComponent<TransformComponent>() != nullptr; });
    }

    void GizmoEditSession::Update(Scene& scene, Vec2 point, bool snapModifier, bool keepAspect) {
        if (!_drag.IsActive()) {
            return;
        }
        const Span<const Gizmo::Item> result = _drag.Update(point, snapModifier, keepAspect);
        bool changed = false;
        for (size_t i = 0; i < _edits.size() && i < result.size(); ++i) {
            if (!(_edits[i].after == result[i])) {
                changed = true;
                break;
            }
        }
        // Nothing is recorded or told when the write fails, so a Commit never carries values the nodes did not get
        if (changed && WriteItems(scene, {_nodes.data(), _nodes.size()}, result)) {
            for (size_t i = 0; i < _edits.size() && i < result.size(); ++i) {
                _edits[i].after = result[i];
            }
            Emit(NodeTransformEditPhase::Change);
        }
    }

    void GizmoEditSession::Finish(Scene& scene) {
        if (!_drag.IsActive()) {
            return;
        }
        const bool moved = eastl::any_of(_edits.begin(), _edits.end(), [](const NodeTransformEdit& edit) { return !(edit.before == edit.after); });
        if (moved) {
            _drag.End();
            _handle = Gizmo::Handle::None;
            Emit(NodeTransformEditPhase::Commit);
        } else {
            Abort(&scene);
        }
    }

    void GizmoEditSession::Abort(Scene* activeScene) {
        if (!_drag.IsActive()) {
            return;
        }
        static_cast<void>(_drag.Cancel());
        _handle = Gizmo::Handle::None;
        // Only nodes still in the tree of the scene of the drag are written to or told; a replaced scene has nothing left to give back
        const bool sameScene = activeScene != nullptr && activeScene == _scene;
        size_t kept = 0;
        for (const NodeTransformEdit& edit : _edits) {
            if (sameScene && HierarchyModel::FindNode(activeScene->GetRootNode(), edit.node)) {
                if (const auto transform = edit.node->GetComponent<TransformComponent>()) {
                    ApplyItem(*transform, edit.before);
                }
                _edits[kept++] = edit;
            }
        }
        _edits.resize(kept);
        Emit(NodeTransformEditPhase::Cancel);
    }

    void GizmoEditSession::Emit(NodeTransformEditPhase phase) const {
        NodeTransformEditEvent::Emit(phase, _scene, Span<const NodeTransformEdit>(_edits.data(), _edits.size()));
    }

    void ViewportGizmos::DrawToolButton(const char* icon, const char* tooltip, const char* id, Gizmo::Tool tool) {
        const bool active = _tool == tool;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, GetImGuiPalette().accent);
        }
        if (IconButton(icon, tooltip, id)) {
            _tool = tool;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    }

    void ViewportGizmos::DrawSnapPopup() {
        if (IconTextButton(ICON_LC_MAGNET, "Snap##ViewportSnapButton", kSnapTooltip)) {
            ImGui::OpenPopup(kSnapPopup);
        }
        if (!ImGui::BeginPopup(kSnapPopup)) {
            return;
        }
        const auto row = [](const char* label, bool& enabled, const char* stepId, float& step, float speed, float maxStep, const char* format) {
            ImGui::Checkbox(label, &enabled);
            ImGui::SameLine();
            const ScopedMonoFont mono;
            ImGui::SetNextItemWidth(kSnapStepWidth);
            ImGui::DragFloat(stepId, &step, speed, kMinSnapStep, maxStep, format);
        };
        row("Position##ViewportSnapPosition", _snap.position, "##ViewportSnapPositionStep", _snap.positionStep, kPositionStepSpeed, kMaxPositionStep, "%.2f units");
        row("Angle##ViewportSnapAngle", _snap.angle, "##ViewportSnapAngleStep", _snap.angleStep, kAngleStepSpeed, kMaxAngleStep, "%.1f deg");
        row("Scale##ViewportSnapScale", _snap.scale, "##ViewportSnapScaleStep", _snap.scaleStep, kScaleStepSpeed, kMaxScaleStep, "%.2f x");
        ImGui::EndPopup();
    }

    void ViewportGizmos::DrawToolbar() {
        DrawToolButton(ICON_LC_MOVE_3D, kMoveTooltip, "ViewportToolMove", Gizmo::Tool::Translate);
        ImGui::SameLine();
        DrawToolButton(ICON_LC_ROTATE_3D, kRotateTooltip, "ViewportToolRotate", Gizmo::Tool::Rotate);
        ImGui::SameLine();
        DrawToolButton(ICON_LC_SCALE_3D, kScaleTooltip, "ViewportToolScale", Gizmo::Tool::Scale);

        ImGui::SameLine();
        const bool world = _space == Gizmo::Space::World;
        if (IconTextButton(world ? ICON_LC_GLOBE : ICON_LC_BOX, world ? "World##ViewportGizmoSpace" : "Local##ViewportGizmoSpace", kSpaceTooltip)) {
            ToggleSpace();
        }

        ImGui::SameLine();
        DrawSnapPopup();
    }

    void ViewportGizmos::ReadKeys() {
        if (_session.IsActive()) {
            return;  // the tool and the space of a drag in progress do not change under it
        }
        if (ViewportKeyPressed(ImGuiKey_W)) {
            _tool = Gizmo::Tool::Translate;
        } else if (ViewportKeyPressed(ImGuiKey_E)) {
            _tool = Gizmo::Tool::Rotate;
        } else if (ViewportKeyPressed(ImGuiKey_R)) {
            _tool = Gizmo::Tool::Scale;
        } else if (ViewportKeyPressed(ImGuiKey_X)) {
            ToggleSpace();
        }
    }

    bool ViewportGizmos::Update(const ViewportGizmoContext& context) {
        _hover = Gizmo::Handle::None;
        if (!context.scene || !context.frame) {
            _session.Abort(context.scene);
            return false;
        }
        const ImGuiIO& io = ImGui::GetIO();
        const Vec2 point = ScenePointOf(context, io.MousePos);

        if (_session.IsActive()) {
            if (!context.editMode || !_session.CanContinue(context.scene) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                _session.Abort(context.scene);
                return false;
            }
            _session.Update(*context.scene, point, io.KeyCtrl, io.KeyShift);
            if (!io.MouseDown[kLeftButton]) {
                _session.Finish(*context.scene);
            }
            if (_session.IsActive()) {
                Draw(context, Gizmo::MakeFrame(_session.Current(), _space), UnitsPerScreenPixel(context));
            }
            return false;
        }

        if (!context.enabled) {
            return false;
        }
        CollectMovable(*context.scene, context.selection, _nodes, _items);
        if (_items.empty()) {
            return false;
        }

        const float unitsPerScreenPixel = UnitsPerScreenPixel(context);
        const Gizmo::Frame gizmoFrame = Gizmo::MakeFrame({_items.data(), _items.size()}, _space);
        if (context.hovered) {
            _hover = Gizmo::HitTest(gizmoFrame, _tool, point, unitsPerScreenPixel);
        }
        Draw(context, gizmoFrame, unitsPerScreenPixel);

        if (_hover != Gizmo::Handle::None && ImGui::IsMouseClicked(kLeftButton)) {
            return _session.Begin(*context.scene, {_nodes.data(), _nodes.size()}, {_items.data(), _items.size()}, _tool, _space, _hover, point, _snap);
        }
        return false;
    }

    void ViewportGizmos::AbortDrag(Scene* activeScene) {
        _session.Abort(activeScene);
    }

    void ViewportGizmos::Draw(const ViewportGizmoContext& context, const Gizmo::Frame& gizmoFrame, float unitsPerScreenPixel) const {
        const ViewportFrame& frame = *context.frame;
        const ViewTransform& view = context.scene->GetViewTransform();
        const Gizmo::Handle shown = _session.IsActive() ? _session.GetHandle() : _hover;
        const auto colorOf = [shown](Gizmo::Handle handle, ImU32 color) { return shown == handle ? kHighlightColor : color; };
        const auto toScreen = [&](Vec2 point) { return SceneToScreen(frame, view, point); };

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        const ImVec2 origin = toScreen(gizmoFrame.origin);
        switch (_tool) {
            case Gizmo::Tool::Translate: {
                const Vec2 axisX = Gizmo::AxisX(gizmoFrame);
                const Vec2 axisY = Gizmo::AxisY(gizmoFrame);
                DrawArrow(*drawList, origin, axisX, colorOf(Gizmo::Handle::AxisX, kAxisXColor));
                DrawArrow(*drawList, origin, axisY, colorOf(Gizmo::Handle::AxisY, kAxisYColor));
                const Vec2 halfX = axisX * Gizmo::kCenterHalfSize;
                const Vec2 halfY = axisY * Gizmo::kCenterHalfSize;
                const ImVec2 corners[] = {{origin.x - halfX.x - halfY.x, origin.y - halfX.y - halfY.y},
                                          {origin.x + halfX.x - halfY.x, origin.y + halfX.y - halfY.y},
                                          {origin.x + halfX.x + halfY.x, origin.y + halfX.y + halfY.y},
                                          {origin.x - halfX.x + halfY.x, origin.y - halfX.y + halfY.y}};
                drawList->AddConvexPolyFilled(corners, static_cast<int>(std::size(corners)), colorOf(Gizmo::Handle::Free, kFreeColor));
                break;
            }
            case Gizmo::Tool::Rotate: {
                drawList->AddCircle(origin, Gizmo::kRingRadius, colorOf(Gizmo::Handle::Ring, kRingColor), 0, kGizmoLineThickness);
                if (_session.IsActive()) {
                    drawList->AddLine(origin, ImGui::GetIO().MousePos, kHighlightColor, kGizmoLineThickness);
                }
                break;
            }
            case Gizmo::Tool::Scale: {
                ImVec2 box[Gizmo::Quad{}.size()];
                for (size_t corner = 0; corner < std::size(box); ++corner) {
                    box[corner] = toScreen(gizmoFrame.box[corner]);
                }
                drawList->AddPolyline(box, static_cast<int>(std::size(box)), kScaleBoxColor, ImDrawFlags_Closed, kScaleBoxThickness);
                const size_t lastHandle = gizmoFrame.isGroup ? static_cast<size_t>(Gizmo::Handle::Corner3) : static_cast<size_t>(Gizmo::Handle::Edge3);
                for (size_t index = static_cast<size_t>(Gizmo::Handle::Corner0); index <= lastHandle; ++index) {
                    const auto handle = static_cast<Gizmo::Handle>(index);
                    const ImVec2 position = toScreen(Gizmo::HandlePosition(gizmoFrame, handle, unitsPerScreenPixel));
                    const ImVec2 low{position.x - kHandleDrawHalfSize, position.y - kHandleDrawHalfSize};
                    const ImVec2 high{position.x + kHandleDrawHalfSize, position.y + kHandleDrawHalfSize};
                    drawList->AddRectFilled(low, high, colorOf(handle, kScaleColor));
                    drawList->AddRect(low, high, kHandleOutlineColor);
                }
                break;
            }
        }
        drawList->PopClipRect();
    }

    void ViewportBoxSelect::Update(const ViewportFrame& frame, Scene* scene, bool enabled, bool hovered, bool pressTaken) {
        const ImGuiIO& io = ImGui::GetIO();
        if (_active) {
            if (!enabled || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                _active = false;
                return;
            }
            const bool isBox = IsBoxSelectDrag(_startScreen.x, _startScreen.y, io.MousePos.x, io.MousePos.y);
            if (!io.MouseDown[kLeftButton]) {
                _active = false;
                if (isBox) {
                    const Vec2 end = frame.ToTarget(io.MousePos);
                    ViewportBoxSelectEvent::Emit(_startTarget.x, _startTarget.y, end.x, end.y, SelectionModifiersOf(io.KeyCtrl, io.KeyShift));
                }
                return;
            }
            if (isBox) {
                const ImGuiPalette& palette = GetImGuiPalette();
                ImVec4 fill = palette.accent;
                fill.w = kRubberBandFillAlpha;
                ImDrawList* const drawList = ImGui::GetWindowDrawList();
                drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
                drawList->AddRectFilled(_startScreen, io.MousePos, ImGui::ColorConvertFloat4ToU32(fill));
                drawList->AddRect(_startScreen, io.MousePos, ImGui::ColorConvertFloat4ToU32(palette.accent), 0.0f, 0, kRubberBandThickness);
                drawList->PopClipRect();
            }
            return;
        }
        if (!enabled || !hovered || pressTaken || !scene || !ImGui::IsMouseClicked(kLeftButton)) {
            return;
        }
        // Only a press on empty ground starts a band: a press on a node is a click on it
        const Vec2 target = frame.ToTarget(io.MousePos);
        if (ScenePick::PickAt(*scene, target.x, target.y).node == nullptr) {
            _active = true;
            _startScreen = io.MousePos;
            _startTarget = target;
        }
    }

}  // namespace BECore
