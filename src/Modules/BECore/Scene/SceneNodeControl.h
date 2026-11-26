#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Scene/SceneControl.h>
#include <cstdint>

namespace BECore {

    /// The answer of `scene_set_node`. `node` looks at the text of the request
    struct SceneSetNodeAnswer {
        BE_CLASS(SceneSetNodeAnswer)

        BE_REFLECT_FIELD int32_t layer = 0;
        BE_REFLECT_FIELD TextView node;
    };

    /**
     * @brief Registers the control API method that writes what a node of a scene holds itself: `scene_set_node`, its draw layer.
     *
     * A node's name has no method: it is a `PoolString`, and a new name would grow the pool with a client's text.
     * Shapes and codes: `.claude/rules/control-scene-api.md`. Registering twice replaces the handler.
     */
    void RegisterSceneNodeControl(ControlService& service, SceneProvider activeScene);

}  // namespace BECore
