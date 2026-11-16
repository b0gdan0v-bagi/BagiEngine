#include "ViewportUIGizmos.h"

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UIAnchorPresets.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILayout.h>
#include <BECore/UI/UIRectEdit.h>
#include <Widgets/SceneEditor/HierarchyModel.h>
#include <Widgets/SceneEditor/ScenePick.h>

namespace BECore {

    namespace {

        bool IsCornerHandle(UIDragHandle handle) {
            return handle == UIDragHandle::TopLeft || handle == UIDragHandle::TopRight || handle == UIDragHandle::BottomRight || handle == UIDragHandle::BottomLeft;
        }

    }  // namespace

    bool UIElementEditSession::Begin(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element) {
        if (_active || node.HasEditorFlag(EditorFlags::Locked)) {
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
        if (!_active || !editMode || activeScene == nullptr || activeScene != _scene) {
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

}  // namespace BECore
