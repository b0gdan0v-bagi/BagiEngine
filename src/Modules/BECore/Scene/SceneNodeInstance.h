#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Reads one `<node>` element into a new SceneNode, as the scene loader reads a node of a scene file
     *
     * A `type` attribute picks the SceneNode subclass of that name through its factory; an unknown one is a deserializer error and a plain node.
     * The node is not wired to any scene: no `OnAttached` (Scene::InstantiateNode does that). A deserializer error
     * (an unknown component type, a malformed value) is one LOG_ERROR that names @p what, and what could be read is kept.
     *
     * @param nodeEl The `<node>` element, usually the copy ExpandNodeReference made
     * @param what Names the node in the log messages (`Scene 'Main': nodes/Hud`)
     */
    IntrusivePtr<SceneNode> DeserializeNode(pugi::xml_node nodeEl, StringView what);

    /// The same from any deserializer positioned at the root scope of a node (a cooked node file): the one place that builds a SceneNode from a reader
    IntrusivePtr<SceneNode> DeserializeNode(IDeserializer& deserializer, StringView what);

    /// What AppendNodeElement leaves out beyond what it always does; the default writes every node and every component field
    struct NodeWriteOptions {
        /// Leaves out every generated descendant (SceneNode::IsGenerated()) with its subtree: the code that made it makes it again at the next load. The node given is always written
        bool skipGenerated = false;

        /// Leaves out every component field that a freshly created component of the same type (AbstractFactory, not initialized, not attached: the state the loader
        /// reads the fields into) serializes the same way, so a file keeps only what the author set and loads back to the same values. A component whose fields are
        /// all defaults is `<component type=".."/>`. A type the factory cannot create writes all its fields; a field of an emptied list is written as an empty element
        bool skipDefaults = false;
    };

    /**
     * @brief Appends to @p parent the `<node>` element of @p node and, in `<children>`, of every node below it: the inline form of a scene file
     *
     * In this order: `type` (only for a SceneNode subclass), `name`, the node's other own fields, `<components>` (each with its serialized fields)
     * and `<children>`, the last two only when there are any, their entries in the tree's order; `<children>` is written only when a child is written.
     * DeserializeNode reads it back.
     *
     * The own fields are whatever the node's virtual `Serialize` writes besides the two lists, so a new reflected field needs nothing here.
     * Whatever @p options says, one that equals that of a freshly made node of the same type is left out (XmlFieldDiff); `name` is always written,
     * `sourceRef` never (a node file link is the writer's business, BuildNodeReference).
     */
    void AppendNodeElement(pugi::xml_node parent, const SceneNode& node, const NodeWriteOptions& options = {});

    /**
     * @brief Appends to @p nodesEl the `<node ref="..">` that stands for @p live: its node file and the smallest overrides
     *
     * The reference is the node's SceneNode::GetSourceRef(). What differs from the node file is the override; what equals it is
     * left out, so an untouched instance is a bare `<node ref="..."/>`.
     *
     * - **Baseline:** the node file expanded without overrides and read with DeserializeNode, so both sides go through the same
     *   serializer and compare whatever spelling the file uses (`color=".."` or `<color>`).
     * - **Node level:** `name` and `layer` are written only when they differ. A reference can override nothing else of the node, so it refuses
     *   when the type or another own field differs from the baseline's.
     * - **Component level:** the live node and the baseline must hold the same component types in the same order (a reference
     *   cannot add, remove or reorder a component), otherwise it refuses. Both sides are serialized and compared field by field:
     *   an attribute by its value, the child elements of one name by their text. A field that differs is written in the spelling of
     *   the live side (an override replaces a field whole in either spelling, see ApplyNodeOverrides); a field only the baseline has
     *   (a list that was emptied and that the serializer leaves out) is written as an empty element of that name. A component that
     *   does not differ writes no `<component>`.
     * - **Round trip:** the reference just built is expanded again and read; if its type, own fields or components do not serialize equal
     *   to the live node's, it refuses. So whatever is written loads back as the live node.
     *
     * @param live The node; it needs a non-empty source reference
     * @param loader Reads the node file
     * @param nodesEl The `<nodes>` element that receives the reference; untouched on false
     * @param reason Receives why the node cannot be written as a reference
     * @return false when it cannot (no source reference, the file cannot be used, the components or the node's own fields do not allow it, the round trip fails)
     */
    bool BuildNodeReference(const SceneNode& live, NodeFileLoaderRef loader, pugi::xml_node nodesEl, String& reason);

}  // namespace BECore
