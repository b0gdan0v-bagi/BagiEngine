#include "ViewportUIGizmos.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/ScenePick.h>
#include <BECore/UI/UIAnchorPresets.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRectEdit.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <imgui.h>

namespace BECore {

    namespace {

        bool IsCornerHandle(UIDragHandle handle) {
            return handle == UIDragHandle::TopLeft || handle == UIDragHandle::TopRight || handle == UIDragHandle::BottomRight || handle == UIDragHandle::BottomLeft;
        }

        constexpr int kLeftButton = 0;

        // Sizes in screen units: a handle and a petal keep their look at any zoom of the viewport
        constexpr float kHandleSize = 9.0f;
        constexpr float kPetalRadius = 6.0f;
        constexpr float kSnapThresholdScreen = 6.0f;
        constexpr float kBodyThickness = 1.5f;
        constexpr float kGuideThickness = 1.0f;
        constexpr float kBoxAlpha = 0.45f;
        constexpr ImU32 kHandleFill = IM_COL32(255, 255, 255, 255);
        constexpr ImU32 kHandleOutline = IM_COL32(0, 0, 0, 220);
        constexpr ImU32 kHoverColor = IM_COL32(255, 220, 70, 255);
        constexpr ImU32 kPetalFill = IM_COL32(255, 255, 255, 235);
        constexpr ImU32 kGuideColor = IM_COL32(255, 70, 200, 230);
        constexpr size_t kCornerCount = EnumUtils<AnchorCorner>::Count();

        Rect OutputRectOf(const UICanvasLayout& layout, const UIElement& element) {
            return layout.GetResolved().transform.ToOutput(element.rect.x, element.rect.y, element.rect.w, element.rect.h);
        }

        ImGuiMouseCursor CursorOf(const UIGrab& grab) {
            if (grab.corner) {
                return ImGuiMouseCursor_Hand;
            }
            switch (grab.handle) {
                case UIDragHandle::TopLeft:
                case UIDragHandle::BottomRight:
                    return ImGuiMouseCursor_ResizeNWSE;
                case UIDragHandle::TopRight:
                case UIDragHandle::BottomLeft:
                    return ImGuiMouseCursor_ResizeNESW;
                case UIDragHandle::Top:
                case UIDragHandle::Bottom:
                    return ImGuiMouseCursor_ResizeNS;
                case UIDragHandle::Left:
                case UIDragHandle::Right:
                    return ImGuiMouseCursor_ResizeEW;
                case UIDragHandle::Body:
                    return ImGuiMouseCursor_ResizeAll;
                case UIDragHandle::None:
                    break;
            }
            return ImGuiMouseCursor_Arrow;
        }

    }  // namespace

    UIGrab ClassifyUIGrab(const Rect& outputRect, const Optional<AnchorMark>& anchors, const Vec2& point, float handleSize, float petalRadius, bool bodyAllowed) {
        UIGrab grab;
        if (anchors) {
            grab.corner = UIElementDrag::HitAnchorCorner(*anchors, point, petalRadius);
            if (grab.corner) {
                return grab;
            }
        }
        grab.handle = UIElementDrag::HitHandle(outputRect, point, handleSize);
        if (grab.handle == UIDragHandle::Body && !bodyAllowed) {
            grab.handle = UIDragHandle::None;
        }
        return grab;
    }

    bool UIElementEditSession::Begin(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element) {
        if (_active || node.HasEditorFlag(EditorFlags::Locked) || !canvas.IsEnabled()) {
            return false;
        }
        const UICanvasLayout& layout = canvas.GetLayout();
        if (UIElementDrag::CanDrag(layout, element) != CanDragReason::Ok) {
            return false;
        }
        const Optional<Rect> box = UIElementDrag::BoxOf(layout, element);
        if (!box) {
            return false;
        }
        _scene = &scene;
        _node = &node;
        _canvas = IntrusivePtr<UICanvasComponent>(&canvas);
        _element = IntrusivePtr<UIElement>(&element);
        _box = *box;
        _startRect = element.rect;
        _start = element.style.rect;
        _current = _start;
        _exact = true;
        _guides = {};
        _active = true;
        Emit(UIRectDragPhase::Begin, &node, _start, _start);
        return true;
    }

    bool UIElementEditSession::BeginHandle(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element, UIDragHandle handle, const Vec2& pointer) {
        if (handle == UIDragHandle::None || _active) {
            return false;
        }
        if (!Begin(scene, node, canvas, element)) {
            return false;
        }
        _anchorDrag = false;
        _handle = handle;
        const auto [canvasX, canvasY] = canvas.GetLayout().OutputToCanvas(pointer.x, pointer.y);
        _startPointer = {canvasX, canvasY};
        return true;
    }

    bool UIElementEditSession::BeginAnchor(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element, AnchorCorner corner) {
        if (_active) {
            return false;
        }
        const Optional<AnchorMark> mark = UIPreviewOverlay::AnchorsOf(canvas.GetLayout(), element);
        if (!mark || !Begin(scene, node, canvas, element)) {
            return false;
        }
        _anchorDrag = true;
        _corner = corner;
        _handle = UIDragHandle::None;
        _mark = *mark;
        return true;
    }

    bool UIElementEditSession::CanContinue(Scene* activeScene, bool editMode) const {
        if (!_active || !editMode || activeScene == nullptr || activeScene != _scene || !_canvas->IsEnabled()) {
            return false;
        }
        const SceneNode* const alive = HierarchyModel::FindNode(activeScene->GetRootNode(), _node);
        if (!alive || alive->HasEditorFlag(EditorFlags::Locked) || alive->GetComponent<UICanvasComponent>().Get() != _canvas.Get()) {
            return false;
        }
        return ScenePick::FindElement(*_canvas, _element.Get()) != nullptr;
    }

    void UIElementEditSession::Update(const UIElementDragInput& input) {
        if (!_active) {
            return;
        }
        UIRectEditResult result;
        if (_anchorDrag) {
            const Optional<Vec2> fraction = UIElementDrag::AnchorFraction(_mark, input.pointer);
            if (!fraction) {
                return;
            }
            const UIEffectiveAnchors start = UILayout::EffectiveAnchors(_start);
            const UIAnchorDragResult anchors = UIElementDrag::DragAnchorCorner(start.min, start.max, _corner, *fraction, input.snap);
            result = UIAnchorPresets::SetAnchors(_box, _start, anchors.min, anchors.max);
            _guides = {};
        } else {
            const auto [canvasX, canvasY] = _canvas->GetLayout().OutputToCanvas(input.pointer.x, input.pointer.y);
            const Vec2 delta{canvasX - _startPointer.x, canvasY - _startPointer.y};
            const bool keepAspect = input.shift && IsCornerHandle(_handle);
            const UIResizeOptions options{keepAspect, input.shift && _handle == UIDragHandle::Body};
            Rect wanted = UIElementDrag::Resize(_startRect, _handle, delta, options);
            // A snapped edge would break the aspect that Shift keeps
            _guides = {};
            if (input.snap && !keepAspect) {
                _guides = UIElementDrag::Snap(wanted, _handle, UIElementDrag::SnapLines(_box), input.snapThreshold);
                wanted = _guides.rect;
            }
            result = UIRectEdit::SpecForRect(_box, _start, wanted);
        }
        if (result.spec == _current) {
            return;
        }
        const UIRectSpec before = _current;
        _element->style.rect = result.spec;
        _canvas->Invalidate();
        _current = result.spec;
        _exact = result.exact;
        Emit(UIRectDragPhase::Change, _node, before, _current);
    }

    void UIElementEditSession::Finish() {
        if (!_active) {
            return;
        }
        if (_current == _start) {
            // Nothing to give back: the element already holds the start spec
            Emit(UIRectDragPhase::Cancel, _node, _current, _start);
            End();
            return;
        }
        Emit(UIRectDragPhase::Commit, _node, _start, _current);
        End();
    }

    void UIElementEditSession::Abort(Scene* activeScene) {
        if (!_active) {
            return;
        }
        // The element is held, so the start can be given back whatever became of the node; the node is named only when it is found
        _element->style.rect = _start;
        _canvas->Invalidate();
        const bool found = activeScene != nullptr && activeScene == _scene && HierarchyModel::FindNode(activeScene->GetRootNode(), _node) != nullptr;
        Emit(UIRectDragPhase::Cancel, found ? _node : nullptr, _current, _start);
        End();
    }

    void UIElementEditSession::Emit(UIRectDragPhase phase, const SceneNode* node, const UIRectSpec& before, const UIRectSpec& after) const {
        UIRectDragEvent::Emit(phase, _scene, node, _element.Get(), before, after, _exact);
    }

    void UIElementEditSession::End() {
        _active = false;
        _anchorDrag = false;
        _handle = UIDragHandle::None;
        _scene = nullptr;
        _node = nullptr;
        _canvas = nullptr;
        _element = nullptr;
    }

    void ViewportUIGizmos::OnSelected(const UIElementSelectedEvent& event) {
        _scene = event.scene;
        _node = event.node;
        _element = event.element;
    }

    UISelectedElement ViewportUIGizmos::FindSelected(Scene* scene) const {
        if (!scene || scene != _scene || !_node || !_element) {
            return {};
        }
        SceneNode* const node = HierarchyModel::FindNode(scene->GetRootNode(), _node);
        IntrusivePtr<UICanvasComponent> canvas = node ? node->GetComponent<UICanvasComponent>() : IntrusivePtr<UICanvasComponent>();
        UIElement* const element = canvas ? ScenePick::FindElement(*canvas, _element) : nullptr;
        if (!element) {
            return {};
        }
        return {node, std::move(canvas), element};
    }

    void ViewportUIGizmos::AbortDrag(Scene* activeScene) {
        _session.Abort(activeScene);
    }

    bool ViewportUIGizmos::Update(const ViewportUIGizmoContext& context) {
        _overHandle = false;
        if (!context.frame) {
            return false;
        }
        const ViewportFrame& frame = *context.frame;
        const ImGuiIO& io = ImGui::GetIO();
        const Vec2 pointer = frame.ToTarget(io.MousePos);

        if (_session.IsActive()) {
            if (!_session.CanContinue(context.scene, context.editMode) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                _session.Abort(context.scene);
                return false;
            }
            UICanvasComponent& canvas = *_session.GetCanvas();
            const float unitsPerOutputPixel = canvas.GetLayout().GetResolved().scale;
            const float threshold = unitsPerOutputPixel > 0.0f ? kSnapThresholdScreen / (frame.scale * unitsPerOutputPixel) : 0.0f;
            _session.Update({pointer, io.KeyShift, !io.KeyCtrl, threshold});
            if (!io.MouseDown[kLeftButton]) {
                _session.Finish();
            }
            if (_session.IsActive()) {
                const UIElement& element = *_session.GetElement();
                UIGrab grabbed;
                if (_session.IsAnchorDrag()) {
                    grabbed.corner = _session.GetCorner();
                } else {
                    grabbed.handle = _session.GetHandle();
                }
                DrawElement(frame, canvas, element, grabbed);
                DrawGuides(frame, canvas, element);
                ImGui::SetMouseCursor(CursorOf(grabbed));
            }
            return false;
        }

        if (!context.editMode || !context.primary || context.primary != _node) {
            return false;
        }
        const UISelectedElement selected = FindSelected(context.scene);
        if (!selected || selected.node->HasEditorFlag(EditorFlags::Locked)) {
            return false;
        }
        SceneNode* const node = selected.node;
        const IntrusivePtr<UICanvasComponent>& canvas = selected.canvas;
        UIElement* const element = selected.element;
        // A World canvas follows its node: its elements are not dragged here
        if (!canvas->IsEnabled() || !canvas->_visible || canvas->_space == CanvasSpace::World) {
            return false;
        }

        const UICanvasLayout& layout = canvas->GetLayout();
        const CanDragReason reason = UIElementDrag::CanDrag(layout, *element);
        if (reason != CanDragReason::Ok) {
            DrawReason(frame, reason);
            return false;
        }

        const Rect outputRect = OutputRectOf(layout, *element);
        const Optional<AnchorMark> anchors = UIPreviewOverlay::AnchorsOf(layout, *element);
        const float handleSize = kHandleSize / frame.scale;
        const float petalRadius = kPetalRadius / frame.scale;
        UIGrab hover;
        if (context.enabled && context.hovered) {
            hover = ClassifyUIGrab(outputRect, anchors, pointer, handleSize, petalRadius, false);
        }
        _overHandle = !hover.IsNone();
        DrawElement(frame, *canvas, *element, hover);
        if (_overHandle) {
            ImGui::SetMouseCursor(CursorOf(hover));
        }

        if (!context.enabled || !context.hovered || !ImGui::IsMouseClicked(kLeftButton)) {
            return false;
        }
        UIGrab grab = hover;
        if (grab.IsNone() && !io.KeyCtrl && !io.KeyShift && ClassifyUIGrab(outputRect, anchors, pointer, handleSize, petalRadius, true).handle == UIDragHandle::Body) {
            // The body is this element's only when the topmost thing at the point is this element: a press on one drawn above it is a normal pick
            if (ScenePick::PickAt(*context.scene, pointer.x, pointer.y).element == element) {
                grab.handle = UIDragHandle::Body;
            }
        }
        if (grab.corner) {
            return _session.BeginAnchor(*context.scene, *node, *canvas, *element, *grab.corner);
        }
        if (grab.handle != UIDragHandle::None) {
            return _session.BeginHandle(*context.scene, *node, *canvas, *element, grab.handle, pointer);
        }
        return false;
    }

    void ViewportUIGizmos::DrawElement(const ViewportFrame& frame, const UICanvasComponent& canvas, const UIElement& element, const UIGrab& hover) const {
        const UICanvasLayout& layout = canvas.GetLayout();
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImU32 accent = ImGui::ColorConvertFloat4ToU32(palette.accent);
        ImVec4 faint = palette.accent;
        faint.w = kBoxAlpha;
        const ImU32 faintAccent = ImGui::ColorConvertFloat4ToU32(faint);
        const auto corners = [&](const Rect& rect) { return Pair<ImVec2, ImVec2>{frame.ToScreen({rect.x, rect.y}), frame.ToScreen({rect.x + rect.w, rect.y + rect.h})}; };

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);

        const Rect outputRect = OutputRectOf(layout, element);
        const auto [bodyMin, bodyMax] = corners(outputRect);
        drawList->AddRect(bodyMin, bodyMax, accent, 0.0f, 0, kBodyThickness);

        const float handleSize = kHandleSize / frame.scale;
        const UIHandleSet handles = UIElementDrag::HandleRects(outputRect, handleSize);
        for (const UIHandleRect& item : handles.View()) {
            const auto [min, max] = corners(item.rect);
            drawList->AddRectFilled(min, max, item.handle == hover.handle ? kHoverColor : kHandleFill);
            drawList->AddRect(min, max, kHandleOutline);
        }

        if (const Optional<AnchorMark> anchors = UIPreviewOverlay::AnchorsOf(layout, element)) {
            const auto [boxMin, boxMax] = corners(anchors->box);
            drawList->AddRect(boxMin, boxMax, faintAccent);
            const auto [anchorMin, anchorMax] = corners(anchors->anchors);
            if (anchorMax.x > anchorMin.x && anchorMax.y > anchorMin.y) {
                drawList->AddRect(anchorMin, anchorMax, faintAccent);
            }
            // A point axis puts the petals of that axis on one spot: one mark is drawn there
            Array<ImVec2, kCornerCount> drawn;
            size_t drawnCount = 0;
            const Optional<Vec2> hoverPoint = hover.corner ? Optional<Vec2>(UIElementDrag::AnchorCornerPoint(*anchors, *hover.corner)) : Optional<Vec2>();
            for (size_t index = 0; index < kCornerCount; ++index) {
                const AnchorCorner corner = static_cast<AnchorCorner>(index);
                const Vec2 point = UIElementDrag::AnchorCornerPoint(*anchors, corner);
                const ImVec2 at = frame.ToScreen(point);
                bool seen = false;
                for (size_t other = 0; other < drawnCount; ++other) {
                    seen = seen || (drawn[other].x == at.x && drawn[other].y == at.y);
                }
                if (seen) {
                    continue;
                }
                drawn[drawnCount++] = at;
                const bool hovered = hoverPoint && hoverPoint->x == point.x && hoverPoint->y == point.y;
                const ImVec2 left{at.x - kPetalRadius, at.y};
                const ImVec2 top{at.x, at.y - kPetalRadius};
                const ImVec2 right{at.x + kPetalRadius, at.y};
                const ImVec2 bottom{at.x, at.y + kPetalRadius};
                drawList->AddQuadFilled(left, top, right, bottom, hovered ? kHoverColor : kPetalFill);
                drawList->AddQuad(left, top, right, bottom, kHandleOutline);
            }
        }
        drawList->PopClipRect();
    }

    void ViewportUIGizmos::DrawGuides(const ViewportFrame& frame, const UICanvasComponent& canvas, const UIElement& element) const {
        const UISnapResult& guides = _session.Guides();
        if (!guides.guideX && !guides.guideY) {
            return;
        }
        const UICanvasLayout& layout = canvas.GetLayout();
        const Optional<Rect> box = UIElementDrag::BoxOf(layout, element);
        if (!box) {
            return;
        }
        const ViewTransform& transform = layout.GetResolved().transform;
        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        if (guides.guideX) {
            const Rect line = transform.ToOutput(*guides.guideX, box->y, 0.0f, box->h);
            drawList->AddLine(frame.ToScreen({line.x, line.y}), frame.ToScreen({line.x, line.y + line.h}), kGuideColor, kGuideThickness);
        }
        if (guides.guideY) {
            const Rect line = transform.ToOutput(box->x, *guides.guideY, box->w, 0.0f);
            drawList->AddLine(frame.ToScreen({line.x, line.y}), frame.ToScreen({line.x + line.w, line.y}), kGuideColor, kGuideThickness);
        }
        drawList->PopClipRect();
    }

    void ViewportUIGizmos::DrawReason(const ViewportFrame& frame, CanDragReason reason) const {
        const char* const text = UIElementDrag::ReasonText(reason);
        const ImGuiPalette& palette = GetImGuiPalette();
        const ImGuiStyle& style = ImGui::GetStyle();
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const ImVec2 pad = style.FramePadding;
        const ImVec2 min(frame.imageMin.x + style.WindowPadding.x, frame.imageMax.y - style.WindowPadding.y - textSize.y - pad.y * 2.0f);
        const ImVec2 max(min.x + textSize.x + pad.x * 2.0f, min.y + textSize.y + pad.y * 2.0f);

        ImDrawList* const drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(frame.imageMin, frame.imageMax, true);
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(palette.accentMuted), style.FrameRounding);
        drawList->AddText({min.x + pad.x, min.y + pad.y}, ImGui::ColorConvertFloat4ToU32(palette.textDisabled), text);
        drawList->PopClipRect();
    }

}  // namespace BECore
