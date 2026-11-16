#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIElementDrag.h>
#include <BECore/UI/UIPreviewOverlay.h>
#include <BECore/UI/UIRectSpec.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <Widgets/Viewport/ViewportEvents.h>
#include <Widgets/Viewport/ViewportGizmos.h>

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

        /// The canvas of the drag (held by the session), null when no drag is active
        [[nodiscard]] UICanvasComponent* GetCanvas() const {
            return _canvas.Get();
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

    /// What a press at one point of the selected element grabs: a corner of the anchor widget, a handle or the body, never both
    struct UIGrab {
        UIDragHandle handle = UIDragHandle::None;
        Optional<AnchorCorner> corner;

        [[nodiscard]] bool IsNone() const {
            return handle == UIDragHandle::None && !corner;
        }
    };

    /**
     * @brief What is under @p point (output pixels): an anchor petal wins over a handle, a handle over the body, and the body counts only with @p bodyAllowed.
     *
     * The petals sit on the anchors rect, which can lie on the corners of the element, so where they overlap the petal is the one the user aimed at. @p bodyAllowed is the caller's answer to "nothing
     * else is on top and no selection key is held": the body is the one grab that a press on another element must not take.
     */
    [[nodiscard]] UIGrab ClassifyUIGrab(const Rect& outputRect, const Optional<AnchorMark>& anchors, const Vec2& point, float handleSize, float petalRadius, bool bodyAllowed);

    /// What ViewportUIGizmos::Update() needs to know about this frame
    struct ViewportUIGizmoContext {
        Scene* scene = nullptr;              ///< the active scene
        const SceneNode* primary = nullptr;  ///< the primary selected node of the scene editor, compared only
        const ViewportFrame* frame = nullptr;
        bool editMode = false;  ///< the input mode is Edit: handles are drawn and a drag in progress ends cleanly when it is not
        bool enabled = false;   ///< the Edit target owns the pointer, no pan and no node gizmo drag is going: a press may grab
        bool hovered = false;   ///< the pointer is over the image
    };

    /**
     * @brief The handles and the anchor widget of the selected canvas element over the viewport, and the drag that moves, resizes or re-anchors it.
     *
     * The maths are UIElementDrag, the writes and the events UIElementEditSession; this is the ImGui layer. The element comes from UIElementSelectedEvent (raw pointers, compared and looked up
     * in the tree of the active scene every frame, never dereferenced before they are found) and must be the primary selection. Update() runs before the node gizmo, which is
     * disabled while IsDragging() or IsOverHandle().
     */
    class ViewportUIGizmos {
    public:
        void OnSelected(const UIElementSelectedEvent& event);

        /// One frame: continues or ends a drag, otherwise draws what the element allows and starts a drag on a press. @return true when the left press of this frame was taken (it is no edit click)
        bool Update(const ViewportUIGizmoContext& context);

        /// Ends a drag in progress as a Cancel. Safe to call with no drag
        void AbortDrag(Scene* activeScene);

        [[nodiscard]] bool IsDragging() const {
            return _session.IsActive();
        }

        /// The pointer was over a handle or a petal in the last Update(): a press there is this gizmo's, so the node gizmo stays out of it
        [[nodiscard]] bool IsOverHandle() const {
            return _overHandle;
        }

    private:
        void DrawElement(const ViewportFrame& frame, const UICanvasComponent& canvas, const UIElement& element, const UIGrab& hover) const;
        void DrawGuides(const ViewportFrame& frame, const UICanvasComponent& canvas, const UIElement& element) const;
        void DrawReason(const ViewportFrame& frame, CanDragReason reason) const;

        const Scene* _scene = nullptr;        // the last UIElementSelectedEvent: compared only
        const SceneNode* _node = nullptr;     // compared, then found in the tree of the active scene
        const UIElement* _element = nullptr;  // compared, then found in the canvas of the node
        UIElementEditSession _session;
        bool _overHandle = false;
    };

}  // namespace BECore
