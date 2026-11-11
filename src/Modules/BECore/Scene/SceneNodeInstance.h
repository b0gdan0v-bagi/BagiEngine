#pragma once

#include <BECore/Scene/SceneNode.h>

namespace BECore {

    /**
     * @brief Reads one `<node>` element into a new SceneNode, as the scene loader reads a node of a scene file
     *
     * The node is not wired to any scene: no entity, no `OnAttached` (Scene::InstantiateNode does that). A deserializer error
     * (an unknown component type, a malformed value) is one LOG_ERROR that names @p what, and what could be read is kept.
     *
     * @param nodeEl The `<node>` element, usually the copy ExpandNodeReference made
     * @param what Names the node in the log messages (`Scene 'Main': nodes/Hud`)
     */
    IntrusivePtr<SceneNode> DeserializeNode(pugi::xml_node nodeEl, eastl::string_view what);

}  // namespace BECore
