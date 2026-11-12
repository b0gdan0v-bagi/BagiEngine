#pragma once

#include "GizmoGeometry.h"

#include <Events/EventBase.h>

namespace BECore {

    class Scene;
    class SceneNode;

    /// The viewport was clicked while it was in the Edit input mode (or with the one-off modifier held): the game got nothing, and the
    /// click is for the editor (to pick a node under it). Emitted once per press, in target pixels (the scene's own units, as the game
    /// would have got them); @p button is the ImGui index: 0 left, 1 right, 2 middle. @p modifiers are the keys held (bits kSelectionModifierCtrl, kSelectionModifierShift of
    /// SceneSelection.h): Ctrl toggles the picked node in the selection, Shift adds it.
    struct ViewportEditClickEvent : public EventBase<ViewportEditClickEvent> {
        BE_EVENT(ViewportEditClickEvent)
        BE_REFLECT_FIELD float x = 0.0f;
        BE_REFLECT_FIELD float y = 0.0f;
        BE_REFLECT_FIELD uint8_t button = 0;
        BE_REFLECT_FIELD uint32_t modifiers = 0;

        ViewportEditClickEvent() = default;
        ViewportEditClickEvent(float clickX, float clickY, uint8_t clickButton, uint32_t clickModifiers = 0) : x(clickX), y(clickY), button(clickButton), modifiers(clickModifiers) {}
    };

    /// A left drag on empty space of the viewport in the Edit input mode ended: the editor selects the nodes the box touches. The corners are in target pixels (as
    /// ViewportEditClickEvent) and come in any order; @p modifiers as for the click (Ctrl or Shift: the nodes are added to the selection, none: they replace it).
    struct ViewportBoxSelectEvent : public EventBase<ViewportBoxSelectEvent> {
        BE_EVENT(ViewportBoxSelectEvent)
        BE_REFLECT_FIELD float x0 = 0.0f;
        BE_REFLECT_FIELD float y0 = 0.0f;
        BE_REFLECT_FIELD float x1 = 0.0f;
        BE_REFLECT_FIELD float y1 = 0.0f;
        BE_REFLECT_FIELD uint32_t modifiers = 0;

        ViewportBoxSelectEvent() = default;
        ViewportBoxSelectEvent(float boxX0, float boxY0, float boxX1, float boxY1, uint32_t boxModifiers = 0) : x0(boxX0), y0(boxY0), x1(boxX1), y1(boxY1), modifiers(boxModifiers) {}
    };

    /// The part of a gizmo drag that an event tells
    CORE_ENUM(NodeTransformEditPhase, uint8_t, Begin, Change, Commit, Cancel)

    /// One node moved by a gizmo drag: its TransformComponent fields before the drag and now
    struct NodeTransformEdit {
        SceneNode* node = nullptr;
        Gizmo::Item before;
        Gizmo::Item after;
    };

    /**
     * @brief A drag of the viewport's gizmo over the selected nodes: the hook for an undo stack and for the "unsaved changes" mark
     *
     * One drag is a @c Begin (the press, @c after equals @c before), a @c Change per frame in which the pointer moved, and then either a @c Commit (the release, only when
     * something moved: the whole drag is one undo step, @c before of the press against @c after of the release) or a @c Cancel (Esc, a scene change, a node that died, the mode flipping to Play:
     * the nodes already hold their @c before again, and @c after is the value that was undone). Emitted, not enqueued. The pointers and @c edits are valid for the frame of the event
     * only: a subscriber that keeps a drag copies the values and finds a node again through the tree before it writes to it.
     */
    struct NodeTransformEditEvent : public EventBase<NodeTransformEditEvent> {
        NodeTransformEditPhase phase = NodeTransformEditPhase::Begin;
        const Scene* scene = nullptr;
        eastl::span<const NodeTransformEdit> edits;

        NodeTransformEditEvent() = default;
        NodeTransformEditEvent(NodeTransformEditPhase editPhase, const Scene* editScene, eastl::span<const NodeTransformEdit> editEdits) : phase(editPhase), scene(editScene), edits(editEdits) {}
    };

}  // namespace BECore
