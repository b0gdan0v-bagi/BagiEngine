#pragma once

#include <Events/EventBase.h>

namespace BECore {

    class Scene;
    class SceneNode;

    /**
     * @brief The node selected in the scene editor changed (a click in the hierarchy, a delete that cleared it, a request to reveal a node, a scene change)
     *
     * Sent by SceneEditorWidget once per change, never every frame. @c node is null when nothing is selected. The pointers are for comparing and for looking
     * up: they are valid for the frame of the event only (a deleted node or a replaced scene leaves them dangling), so a subscriber that keeps one must reset
     * it on the next event. Emitted, not enqueued: a handler runs inside the frame.
     */
    struct SceneNodeSelectedEvent : public EventBase<SceneNodeSelectedEvent> {
        const Scene* scene = nullptr;
        const SceneNode* node = nullptr;

        SceneNodeSelectedEvent() = default;
        SceneNodeSelectedEvent(const Scene* s, const SceneNode* n) : scene(s), node(n) {}
    };

    /**
     * @brief Asks the scene editor to select a node and show it in the hierarchy
     *
     * SceneEditorWidget answers only for a node that is alive in the tree of the active scene (it walks the tree, so a stale pointer is never dereferenced);
     * anything else is ignored. The node becomes the selection, its ancestors open, the row scrolls into view and a search filter that hides it is cleared.
     * A locked node can be revealed; it stays read-only.
     */
    struct SelectSceneNodeRequestEvent : public EventBase<SelectSceneNodeRequestEvent> {
        const Scene* scene = nullptr;
        const SceneNode* node = nullptr;

        SelectSceneNodeRequestEvent() = default;
        SelectSceneNodeRequestEvent(const Scene* s, const SceneNode* n) : scene(s), node(n) {}
    };

}  // namespace BECore
