#pragma once

#include <cstddef>
#include <cstdint>

namespace BECore {

    class Scene;
    class SceneNode;

    /// The place of one node in the draw order: a component of the node's entity in the scene's registry. Written when the draw list is rebuilt.
    struct SceneDrawItem {
        int32_t layer = 0;   ///< SceneNode::GetLayer() at the rebuild
        uint32_t order = 0;  ///< index of the node in the depth-first walk of the tree (the scene order)
        SceneNode* node = nullptr;
        int32_t rank = 0;                 ///< index in the sorted storage: how many nodes draw before this one (Scene::GetDrawRank())
        bool hidden = false;              ///< the node or an ancestor has EditorFlags::Hidden: the node keeps its place in the order but draws nothing
        const SceneNode* clip = nullptr;  ///< the nearest self-or-ancestor with a ClipRectComponent and a TransformComponent, or null
        int32_t clipIndex = -1;           ///< index of that owner in SceneDrawList::clips, -1 for none
    };

    /**
     * @brief The draw pass of a scene: a loop over the dense draw list.
     *
     * The list (SceneDrawList, a member of the scene) holds one entry per drawing component of a visible node, in draw order. Run() rebuilds
     * it when Scene::IsDrawListValid() is false, that is after a node was added, removed, moved or put on another layer, a component was
     * added or removed, or the editor flags changed; a steady frame only loops. The rebuild writes a SceneDrawItem for every node (the
     * order and the rank, kept in the registry so GetDrawRank() stays O(1)), sorts them by (layer, scene order), writes each item's `rank`
     * before any OnDraw() so a component can ask Scene::GetDrawRank(), and then fills the entries from the sorted storage. The order is
     * the one documented at Scene::Draw().
     *
     * The quad and the sprite are drawn by the system itself and read the fields of their components at draw time, so a change shows in the
     * next draw with no event or setter hook (a quad or a sprite without a TransformComponent, a second one on the node and a sprite without
     * a texture draw nothing); every other component with IComponent::HasOnDraw() draws itself through IComponent::OnDraw(), at the place of
     * the component in its node.
     *
     * A hidden node (SceneNode::GetEditorFlags()) and everything below it keep their item and their rank but have no entry; they still update.
     *
     * A node with a rotation or a sprite flip is drawn with DrawTransformedRect() / DrawTransformedTexture() about the pivot of
     * the node's rect. The quad's or sprite's blend mode is set on the renderer
     * around that one draw only (ScopedBlendMode), so the next node and every OnDraw() see Alpha again.
     *
     * A ClipRectComponent clips its node and the subtree, whatever the layers of the nodes in it: each entry names its clip owner by index
     * into SceneDrawList::clips, which chain to the enclosing owners. While the loop goes from entry to entry the system pushes the owner's chain on the
     * renderer (outermost first, the transform rects through the scene's view transform) and pops what no longer applies. Nothing stays
     * pushed when Run() returns.
     */
    class SceneDrawSystem {
    public:
        /// The deepest chain of nested clip owners that is applied; a deeper one loses its outermost clips
        static constexpr size_t kMaxClipDepth = 8;

        /// Draws @p scene, whose root node must exist. Nothing may add or remove nodes or components while this runs.
        static void Run(Scene& scene);
    };

}  // namespace BECore
