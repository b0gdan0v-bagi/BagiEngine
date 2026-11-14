#pragma once

#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>

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
    IntrusivePtr<SceneNode> DeserializeNode(pugi::xml_node nodeEl, StringView what);

    /**
     * @brief Appends to @p parent the `<node>` element of @p node and, in `<children>`, of every node below it: the inline form of a scene file
     *
     * `name`, `layer`, `<components>` (each with its serialized fields) and `<children>`, the last two only when there are any; the order is
     * the tree's. `sourceRef` is not written (a node file link is the writer's business, BuildNodeReference). DeserializeNode reads it back.
     */
    void AppendNodeElement(pugi::xml_node parent, const SceneNode& node);

    /**
     * @brief Appends to @p nodesEl the `<node ref="..">` that stands for @p live: its node file and the smallest overrides
     *
     * The reference is the node's SceneNode::GetSourceRef(). What differs from the node file is the override; what equals it is
     * left out, so an untouched instance is a bare `<node ref="..."/>`.
     *
     * - **Baseline:** the node file expanded without overrides and read with DeserializeNode, so both sides go through the same
     *   serializer and compare whatever spelling the file uses (`color=".."` or `<color>`).
     * - **Node level:** `name` and `layer` are written only when they differ.
     * - **Component level:** the live node and the baseline must hold the same component types in the same order (a reference
     *   cannot add, remove or reorder a component), otherwise it refuses. Both sides are serialized and compared field by field:
     *   an attribute by its value, the child elements of one name by their text. A field that differs is written in the spelling of
     *   the live side (an override replaces a field whole in either spelling, see ApplyNodeOverrides); a field only the baseline has
     *   (a list that was emptied and that the serializer leaves out) is written as an empty element of that name. A component that
     *   does not differ writes no `<component>`.
     * - **Round trip:** the reference just built is expanded again and read; if its components, name or layer do not serialize equal
     *   to the live node's, it refuses. So whatever is written loads back as the live node.
     *
     * @param live The node; it needs a non-empty source reference
     * @param loader Reads the node file
     * @param nodesEl The `<nodes>` element that receives the reference; untouched on false
     * @param reason Receives why the node cannot be written as a reference
     * @return false when it cannot (no source reference, the file cannot be used, the components do not match, the round trip fails)
     */
    bool BuildNodeReference(const SceneNode& live, const NodeFileLoader& loader, pugi::xml_node nodesEl, String& reason);

}  // namespace BECore
