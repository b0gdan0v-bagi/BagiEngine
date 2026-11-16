#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIElementDrag.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/UI/UIRectSpec.h>
#include <Widgets/Viewport/ViewportEvents.h>

namespace BECore {

    class Scene;
    class SceneNode;
    class UICanvasComponent;

    /// What the pointer and the keys say in one frame of a drag of a canvas element
    struct UIElementDragInput {
        Vec2 pointer;                ///< render output pixels, as ViewportFrame::ToTarget gives them
        bool shift = false;          ///< a corner resize keeps the aspect, a move keeps one axis
        bool snap = true;            ///< false while Ctrl is held
        float snapThreshold = 0.0f;  ///< canvas units
    };

    /**
     * @brief One drag of a canvas element in the viewport: the writes into `style.rect` and UIRectDragEvent. No ImGui.
     *
     * The drag is of a handle (move or resize, UIDragHandle) or of a corner of the anchor widget (AnchorCorner). Begin* takes the element when it can be dragged (UIElementDrag::CanDrag) and tells
     * @c Begin; Update() turns the pointer into a wanted rect (UIElementDrag::Resize, Snap) or into new anchors (DragAnchorCorner), the wanted rect into a spec (UIRectEdit::SpecForRect,
     * UIAnchorPresets::SetAnchors) and writes it, always from the spec at the press and the pointer, never from the previous result; it tells @c Change in a call where the spec differs from
     * the last one. Finish() ends at the release with a @c Commit when the spec differs from the start and with a @c Cancel when it does not; Abort() gives the start spec back and tells @c Cancel.
     *
     * The scene is compared only and the node is never touched before it is found in the tree of the active scene (CanContinue()), so a node deleted by anything else ends the drag instead of being
     * read. The canvas and the element are held, which is why the start spec can always be given back.
     */
    class UIElementEditSession {
    public:
        /// Starts the drag of @p handle of @p element, pressed at @p pointer. False (nothing started) for UIDragHandle::None, an element that cannot be dragged or a node the editor locked
        bool BeginHandle(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element, UIDragHandle handle, const Vec2& pointer);

        /// Starts the drag of @p corner of the anchor widget of @p element. False for an element that cannot be dragged, has no anchors or a locked node
        bool BeginAnchor(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element, AnchorCorner corner);

        /// True while the drag can go on in @p activeScene: it is the scene of the drag, the input mode is Edit, the node is in its tree and not locked, still has the canvas, and the element is in
        /// its tree
        [[nodiscard]] bool CanContinue(Scene* activeScene, bool editMode) const;

        /// One frame, after CanContinue(). Writes `style.rect`, invalidates the canvas and tells @c Change when the spec differs from the last one
        void Update(const UIElementDragInput& input);

        /// The release: @c Commit when the spec differs from the start, otherwise a @c Cancel
        void Finish();

        /// Gives back the start spec and tells @c Cancel. Nothing for no drag. The node is named in the event only when it is still in the tree of @p activeScene
        void Abort(Scene* activeScene);

        [[nodiscard]] bool IsActive() const {
            return _active;
        }

        [[nodiscard]] bool IsAnchorDrag() const {
            return _anchorDrag;
        }

        [[nodiscard]] UIDragHandle GetHandle() const {
            return _handle;
        }

        [[nodiscard]] AnchorCorner GetCorner() const {
            return _corner;
        }

        /// The element of the drag, null when no drag is active; compare it
        [[nodiscard]] const UIElement* GetElement() const {
            return _element.Get();
        }

        /// The authored spec of the last frame and whether the layout limits kept it from laying out where the pointer wanted
        [[nodiscard]] const UIRectSpec& Current() const {
            return _current;
        }

        [[nodiscard]] bool IsExact() const {
            return _exact;
        }

        /// The snap lines (canvas units) the last Update() put the element on; none on an axis it did not snap on
        [[nodiscard]] const UISnapResult& Guides() const {
            return _guides;
        }

    private:
        bool Begin(Scene& scene, SceneNode& node, UICanvasComponent& canvas, UIElement& element);
        void Emit(UIRectDragPhase phase, const SceneNode* node, const UIRectSpec& before, const UIRectSpec& after) const;
        void End();

        bool _active = false;
        bool _anchorDrag = false;
        const Scene* _scene = nullptr;     // compared with the active scene, never dereferenced
        const SceneNode* _node = nullptr;  // compared and looked up in the tree of the active scene, never dereferenced before it is found
        IntrusivePtr<UICanvasComponent> _canvas;
        IntrusivePtr<UIElement> _element;
        UIDragHandle _handle = UIDragHandle::None;
        AnchorCorner _corner = AnchorCorner::TopLeft;
        UIRectSpec _start;
        UIRectSpec _current;
        bool _exact = true;
        Rect _startRect;     // canvas units
        Rect _box;           // the box the element lays out in, canvas units
        Vec2 _startPointer;  // canvas units
        AnchorMark _mark;    // the anchor widget at the press, for the fraction of the pointer
        UISnapResult _guides;
    };

}  // namespace BECore
