#pragma once

namespace BECore {

    class Scene;
    class SceneNode;

    /// The place of one node in the draw pass: a component of the node's entity in the scene's registry. Written by SceneDrawSystem::Run().
    struct SceneDrawItem {
        int32_t layer = 0;   ///< SceneNode::GetLayer() when the pass started
        uint32_t order = 0;  ///< index of the node in the depth-first walk of the tree (the scene order)
        SceneNode* node = nullptr;
    };

    /**
     * @brief The draw pass of a scene as a system over the registry.
     *
     * Run() writes a SceneDrawItem for every node of the tree, sorts that storage by (layer, order) and walks a view over
     * it, calling IComponent::OnDraw() of each node's components in component order. The order is the one documented at
     * Scene::Draw(). The view is the only source of the order: nothing keeps a second list of the nodes.
     */
    class SceneDrawSystem {
    public:
        /// Draws @p scene, whose root node must exist. Nothing may add or remove nodes while this runs.
        static void Run(Scene& scene);
    };

}  // namespace BECore
