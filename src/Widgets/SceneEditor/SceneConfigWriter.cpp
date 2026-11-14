#include "SceneConfigWriter.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore {

    namespace {

        constexpr const char* kNoLoaderReason = "no node file loader was given";
        constexpr const char* kHasChildrenReason = "it has child nodes";

        /// A line in @p notes for every node below @p node that is an instance of a node file: nested nodes are written inline (a reference has no
        /// place among the children), so their link is lost
        void NoteNestedLinks(const SceneNode& node, eastl::vector<String>& notes) {
            for (const auto& child : node.GetChildren()) {
                if (!child->GetSourceRef().Empty()) {
                    notes.push_back(Format("node '{}': written inline, it is nested and its link to the node file '{}' is not kept", child->GetName().CStr(), child->GetSourceRef().CStr()));
                }
                NoteNestedLinks(*child, notes);
            }
        }

        /// The `<scene>` of @p scenesEl whose `name` is @p name, or an invalid node
        pugi::xml_node FindSceneElement(pugi::xml_node scenesEl, StringView name) {
            for (pugi::xml_node el = scenesEl.child("scene"); el; el = el.next_sibling("scene")) {
                if (StringView{el.attribute("name").as_string()} == name) {
                    return el;
                }
            }
            return {};
        }

        /// One top-level node into @p nodesEl: an instance of a node file as its `<node ref>` with the overrides, any other node (and an instance that
        /// cannot be written that way, with a line in @p notes) inline. An instance with children is inline too: the override language has none
        void AppendNode(pugi::xml_node nodesEl, const SceneNode& node, const NodeFileLoader* loader, eastl::vector<String>* notes) {
            if (!node.GetSourceRef().Empty()) {
                String reason;
                if (!node.GetChildren().empty()) {
                    reason = kHasChildrenReason;
                } else if (loader) {
                    if (BuildNodeReference(node, *loader, nodesEl, reason)) {
                        return;
                    }
                } else {
                    reason = kNoLoaderReason;
                }
                if (notes) {
                    notes->push_back(Format("node '{}': written inline, {}", node.GetName().CStr(), reason.c_str()));
                }
            }
            AppendNodeElement(nodesEl, node);
            if (notes) {
                NoteNestedLinks(node, *notes);
            }
        }

    }  // namespace

    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene, const NodeFileLoader* loader, eastl::vector<String>* notes) {
        if (!configRoot) {
            return false;
        }

        scene.PrepareForSave();

        pugi::xml_node scenesEl = configRoot.child("scenes");
        if (!scenesEl) {
            scenesEl = configRoot.append_child("scenes");
        }

        const pugi::xml_node oldEl = FindSceneElement(scenesEl, scene.GetName().CStr());

        // The new element is built next to the old one and the old one is removed last, so the scene keeps its place
        pugi::xml_node sceneEl = oldEl ? scenesEl.insert_child_before("scene", oldEl) : scenesEl.append_child("scene");
        sceneEl.append_attribute("type").set_value(scene.GetTypeMeta().typeName.data());

        // The attributes of the serialized scene and its other children (`viewVariants`) are taken. Its <nodes> child is left behind
        // on purpose: Serialize writes every node with `enabled` and `type` and every layer, and the file keeps its shorter shape (AppendNodeElement)
        XmlSerializer s;
        scene.Serialize(s);
        const auto& srcRoot = s.GetRootNode();
        for (auto attr = srcRoot.first_attribute(); attr; attr = attr.next_attribute()) {
            sceneEl.append_attribute(attr.name()).set_value(attr.value());
        }
        for (auto child = srcRoot.first_child(); child; child = child.next_sibling()) {
            if (StringView{child.name()} != "nodes") {
                sceneEl.append_copy(child);
            }
        }

        pugi::xml_node nodesEl = sceneEl.append_child("nodes");
        for (const auto& nodePtr : scene.GetRootNode().GetChildren()) {
            AppendNode(nodesEl, *nodePtr.Get(), loader, notes);
        }

        if (oldEl) {
            scenesEl.remove_child(oldEl);
        }
        return true;
    }

}  // namespace BECore
