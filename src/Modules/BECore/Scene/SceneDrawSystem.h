#pragma once

#include <cstddef>
#include <cstdint>

namespace BECore {

    class Scene;
    class SceneNode;

    /// The place of one node in the draw pass: a component of the node's entity in the scene's registry. Written by SceneDrawSystem::Run().
    struct SceneDrawItem {
        int32_t layer = 0;   ///< SceneNode::GetLayer() when the pass started
        uint32_t order = 0;  ///< index of the node in the depth-first walk of the tree (the scene order)
        SceneNode* node = nullptr;
        int32_t rank = 0;                 ///< index in the sorted storage: how many nodes draw before this one (Scene::GetDrawRank())
        bool hidden = false;              ///< the node or an ancestor has EditorFlags::Hidden: the node keeps its place in the order but draws nothing
        const SceneNode* clip = nullptr;  ///< the nearest self-or-ancestor with a ClipRectComponent and a TransformComponent, or null
    };

    /**
     * @brief The draw pass of a scene as a system over the registry.
     *
     * Run() writes a SceneDrawItem for every node of the tree, sorts that storage by (layer, order), writes each item's `rank` (its index
     * in the sorted storage, before any OnDraw() so a component can ask Scene::GetDrawRank()) and walks a view over it. The order is the
     * one documented at Scene::Draw(); nothing keeps a second list of the nodes.
     *
     * The same walk mirrors TransformComponent, QuadRendererComponent and SpriteRendererComponent into the registry as
     * TransformData, QuadDrawData and SpriteDrawData (SceneRenderData.h); the components stay the source of truth.
     *
     * For each node the view yields, Run() goes through its components in component order. The quad and the sprite are
     * drawn by the system itself from that registry data (a quad or a sprite without a TransformData and a sprite without a
     * texture draw nothing); every other component draws itself through IComponent::OnDraw().
     *
     * A hidden node (SceneNode::GetEditorFlags()) and everything below it keep their item and their rank but draw nothing, neither the quad
     * and the sprite nor OnDraw(); they still update.
     *
     * A node with a rotation or a sprite flip is drawn with DrawTransformedRect() / DrawTransformedTexture() about the pivot of
     * the node's rect. The quad's or sprite's blend mode is set on the renderer
     * around that one draw only (ScopedBlendMode), so the next node and every OnDraw() see Alpha again.
     *
     * A ClipRectComponent clips its node and the subtree, whatever the layers of the nodes in it: each item names its clip owner, and
     * while the walk goes from item to item the system pushes the owner's chain on the renderer (outermost first, the transform rects
     * through the scene's view transform) and pops what no longer applies. Nothing stays pushed when Run() returns.
     */
    class SceneDrawSystem {
    public:
        /// The deepest chain of nested clip owners that is applied; a deeper one loses its outermost clips
        static constexpr size_t kMaxClipDepth = 8;

        /// Draws @p scene, whose root node must exist. Nothing may add or remove nodes while this runs.
        static void Run(Scene& scene);
    };

}  // namespace BECore
