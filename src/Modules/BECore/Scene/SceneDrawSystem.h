#pragma once

namespace BECore {

    class Scene;
    class SceneNode;

    /// The place of one node in the draw pass: a component of the node's entity in the scene's registry. Written by SceneDrawSystem::Run().
    struct SceneDrawItem {
        int32_t layer = 0;   ///< SceneNode::GetLayer() when the pass started
        uint32_t order = 0;  ///< index of the node in the depth-first walk of the tree (the scene order)
        SceneNode* node = nullptr;
        bool hidden = false;  ///< the node or an ancestor has EditorFlags::Hidden: the node keeps its place in the order but draws nothing
    };

    /**
     * @brief The draw pass of a scene as a system over the registry.
     *
     * Run() writes a SceneDrawItem for every node of the tree, sorts that storage by (layer, order) and walks a view over
     * it. The order is the one documented at Scene::Draw(). The view is the only source of the order: nothing keeps a
     * second list of the nodes.
     *
     * The same walk mirrors TransformComponent, QuadRendererComponent and SpriteRendererComponent into the registry as
     * TransformData, QuadDrawData and SpriteDrawData (SceneRenderData.h); the components stay the source of truth.
     *
     * For each node the view yields, Run() goes through its components in component order. The quad and the sprite are
     * drawn by the system itself from that registry data (DrawFilledRect(), FitSpriteRect() and DrawTexture()), a quad or
     * a sprite without a TransformData and a sprite without a texture draw nothing; every other component draws itself
     * through IComponent::OnDraw().
     *
     * A hidden node (SceneNode::GetEditorFlags()) and everything below it keep their item and their rank but draw nothing, neither the quad
     * and the sprite nor OnDraw(); they still update.
     *
     * A node with a rotation or a sprite flip is drawn with DrawTransformedRect() / DrawTransformedTexture() about the pivot of
     * the node's rect; with neither it makes the plain calls above. The quad's or sprite's blend mode is set on the renderer
     * around that one draw only (ScopedBlendMode), so the next node and every OnDraw() see Alpha again.
     */
    class SceneDrawSystem {
    public:
        /// Draws @p scene, whose root node must exist. Nothing may add or remove nodes while this runs.
        static void Run(Scene& scene);
    };

}  // namespace BECore
