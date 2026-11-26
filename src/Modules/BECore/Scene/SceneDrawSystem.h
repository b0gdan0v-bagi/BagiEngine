#pragma once

#include <cstddef>

namespace BECore {

    class Scene;

    /**
     * @brief The draw pass of a scene: a loop over the dense draw list.
     *
     * The list (SceneDrawList, a member of the scene) holds one entry per drawing component of a visible node, in draw order. Run() rebuilds
     * it when Scene::IsDrawListValid() is false, that is after a node was added, removed, moved or put on another layer, a component was
     * added or removed, or the editor flags changed; a steady frame only loops. The rebuild writes a SceneDrawItem for every node into
     * SceneDrawList::items, sorts them by (layer, scene order), writes each node's rank (its index in the sorted items) before any OnDraw()
     * so a component can ask Scene::GetDrawRank() in O(1), and then fills the entries from the sorted items. The order is
     * the one documented at Scene::Draw().
     *
     * The quad and the sprite are drawn by the system itself and read the fields of their components at draw time, so a change shows in the
     * next draw with no event or setter hook (a quad or a sprite without a TransformComponent, a second one on the node and a sprite without
     * a texture draw nothing); every other component with IComponent::HasOnDraw() draws itself through IComponent::OnDraw(), at the place of
     * the component in its node.
     *
     * A hidden node (SceneNode::GetEditorFlags()) and everything below it keep their item and their rank but have no entry; they still update.
     * A disabled component (IComponent::IsEnabled()) keeps its entry, which the loop passes over.
     *
     * A node with a rotation or a sprite flip is drawn with DrawTransformedRect() / DrawTransformedTexture() about the pivot of
     * the node's rect. The quad's or sprite's blend mode is set on the renderer
     * around that one draw only (ScopedBlendMode), so the next node and every OnDraw() see Alpha again.
     *
     * A ClipRectComponent clips its node and the subtree, whatever the layers of the nodes in it: each entry names its clip owner by index
     * into SceneDrawList::clips, which chain to the enclosing owners. While the loop goes from entry to entry the system pushes the owner's chain on the
     * renderer (outermost first, the transform rects through the scene's view transform) and pops what no longer applies; the owner of a
     * disabled ClipRectComponent is left out of the chain. Nothing stays pushed when Run() returns.
     */
    class SceneDrawSystem {
    public:
        /// The deepest chain of nested clip owners that is applied; a deeper one loses its outermost clips
        static constexpr size_t kMaxClipDepth = 8;

        /// Draws @p scene, whose root node must exist. Nothing may add or remove nodes or components while this runs.
        static void Run(Scene& scene);
    };

}  // namespace BECore
