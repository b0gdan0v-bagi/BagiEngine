#pragma once

#include <Events/EventBase.h>

namespace BECore {

    class Scene;
    class SceneNode;

    /**
     * @brief Asks the Add Node dialog (`SceneDialogsModel`) to open under @c parent, the root of @c scene when null
     *
     * Sent by SceneEditorWidget for the hierarchy's "Add" menu items. Emitted, not enqueued. The pointers are valid for the frame of the event; the model keeps them
     * and finds the node in the tree of the active scene again every frame, closing the dialog when it is gone.
     */
    struct SceneAddNodeRequestEvent : public EventBase<SceneAddNodeRequestEvent> {
        Scene* scene = nullptr;
        SceneNode* parent = nullptr;

        SceneAddNodeRequestEvent() = default;
        SceneAddNodeRequestEvent(Scene* s, SceneNode* p) : scene(s), parent(p) {}
    };

    /**
     * @brief Asks the Add Component dialog to open for @c node; a null or a locked node opens nothing
     *
     * Sent by the inspector's Add Component button. Pointers as in SceneAddNodeRequestEvent.
     */
    struct SceneAddComponentRequestEvent : public EventBase<SceneAddComponentRequestEvent> {
        Scene* scene = nullptr;
        SceneNode* node = nullptr;

        SceneAddComponentRequestEvent() = default;
        SceneAddComponentRequestEvent(Scene* s, SceneNode* n) : scene(s), node(n) {}
    };

    /**
     * @brief The Add Node dialog added the authored node @c node under @c parent (never null: the root when the request named none)
     *
     * SceneEditorWidget answers with the unsaved mark and the reveal of @c parent. The pointers are
     * valid for the frame of the event. A component added by the Add Component dialog is reported with SceneEditedEvent.
     */
    struct SceneNodeAddedEvent : public EventBase<SceneNodeAddedEvent> {
        const Scene* scene = nullptr;
        SceneNode* parent = nullptr;
        const SceneNode* node = nullptr;

        SceneNodeAddedEvent() = default;
        SceneNodeAddedEvent(const Scene* s, SceneNode* p, const SceneNode* n) : scene(s), parent(p), node(n) {}
    };

}  // namespace BECore
