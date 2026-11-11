#include "SceneConfigWriter.h"

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore {

    namespace {

        constexpr const char* kNoLoaderReason = "no node file loader was given";

        void BuildNodeXml(pugi::xml_node parent, const SceneNode& node) {
            pugi::xml_node nodeEl = parent.append_child("node");
            nodeEl.append_attribute("name").set_value(node.GetName().CStr());
            if (node.GetLayer() != 0) {
                nodeEl.append_attribute("layer").set_value(node.GetLayer());
            }

            const auto& components = node.GetComponents();
            if (components.empty()) {
                return;
            }

            pugi::xml_node compsEl = nodeEl.append_child("components");
            for (const auto& comp : components) {
                pugi::xml_node compEl = compsEl.append_child("component");
                const auto& typeMeta = comp->GetTypeMeta();
                compEl.append_attribute("type").set_value(typeMeta.typeName.data());

                XmlSerializer s;
                comp->Serialize(s);
                const auto& srcRoot = s.GetRootNode();
                for (auto attr = srcRoot.first_attribute(); attr; attr = attr.next_attribute())
                    compEl.append_attribute(attr.name()).set_value(attr.value());
                for (auto child = srcRoot.first_child(); child; child = child.next_sibling())
                    compEl.append_copy(child);
            }
        }

        /// The `<scene>` of @p scenesEl whose `name` is @p name, or an invalid node
        pugi::xml_node FindSceneElement(pugi::xml_node scenesEl, eastl::string_view name) {
            for (pugi::xml_node el = scenesEl.child("scene"); el; el = el.next_sibling("scene")) {
                if (eastl::string_view{el.attribute("name").as_string()} == name) {
                    return el;
                }
            }
            return {};
        }

        /// One top-level node into @p nodesEl: an instance of a node file as its `<node ref>` with the overrides, any other node (and an instance that
        /// cannot be written that way, with a line in @p notes) inline
        void AppendNode(pugi::xml_node nodesEl, const SceneNode& node, const NodeFileLoader* loader, eastl::vector<eastl::string>* notes) {
            if (!node.GetSourceRef().Empty()) {
                eastl::string reason;
                if (loader) {
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
            BuildNodeXml(nodesEl, node);
        }

    }  // namespace

    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene, const NodeFileLoader* loader, eastl::vector<eastl::string>* notes) {
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
        // on purpose: Serialize writes every node with `enabled` and `type` and every layer, and the file keeps its shorter shape (BuildNodeXml)
        XmlSerializer s;
        scene.Serialize(s);
        const auto& srcRoot = s.GetRootNode();
        for (auto attr = srcRoot.first_attribute(); attr; attr = attr.next_attribute()) {
            sceneEl.append_attribute(attr.name()).set_value(attr.value());
        }
        for (auto child = srcRoot.first_child(); child; child = child.next_sibling()) {
            if (eastl::string_view{child.name()} != "nodes") {
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
