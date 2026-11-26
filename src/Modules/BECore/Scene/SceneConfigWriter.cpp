#include "SceneConfigWriter.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore {

    namespace {

        constexpr StringView kNameAttribute = "name";
        constexpr const char* kNoLoaderReason = "no node file loader was given";
        constexpr const char* kHasChildrenReason = "it has child nodes";

        constexpr NodeWriteOptions kWriteOptions{.skipGenerated = true, .skipDefaults = true};

        bool HasAuthoredChildren(const SceneNode& node) {
            for (const auto& child : node.GetChildren()) {
                if (!child->IsGenerated()) {
                    return true;
                }
            }
            return false;
        }

        /// A line in @p notes for every written node below @p node that is an instance of a node file: nested nodes are written inline (a reference has no
        /// place among the children), so their link is lost
        void NoteNestedLinks(const SceneNode& node, Vector<String>& notes) {
            for (const auto& child : node.GetChildren()) {
                if (child->IsGenerated()) {
                    continue;
                }
                if (!child->GetSourceRef().Empty()) {
                    notes.push_back(Format("node '{}': written inline, it is nested and its link to the node file '{}' is not kept", child->GetName().CStr(), child->GetSourceRef().CStr()));
                }
                NoteNestedLinks(*child, notes);
            }
        }

        /// A line in @p notes for the topmost authored node of every subtree whose parent was made at run time: the parent is not written, so neither is the node
        void NoteOrphans(const SceneNode& node, bool underGenerated, Vector<String>& notes) {
            for (const auto& child : node.GetChildren()) {
                if (!child->IsGenerated() && underGenerated) {
                    notes.push_back(Format("node '{}': not saved, its parent was made at run time", child->GetName().CStr()));
                } else {
                    NoteOrphans(*child, child->IsGenerated(), notes);
                }
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
        /// cannot be written that way, with a line in @p notes) inline. An instance with authored children is inline too: the override language has none
        void AppendNode(pugi::xml_node nodesEl, const SceneNode& node, const NodeFileLoader* loader, Vector<String>* notes) {
            if (!node.GetSourceRef().Empty()) {
                String reason;
                if (HasAuthoredChildren(node)) {
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
            AppendNodeElement(nodesEl, node, kWriteOptions);
            if (notes) {
                NoteNestedLinks(node, *notes);
            }
        }

    }  // namespace

    bool WriteSceneToConfig(pugi::xml_node configRoot, Scene& scene, const NodeFileLoader* loader, Vector<String>* notes) {
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
        // on purpose: Serialize writes every node with its `type` and every default, and the file keeps its shorter shape (AppendNodeElement)
        XmlSerializer s;
        scene.Serialize(s);
        const auto& srcRoot = s.GetRootNode();
        // A default Scene serializes the attributes of an unedited one: an attribute that has the same value there loads as that value, so the file leaves it out
        const IntrusivePtr<Scene> defaults = New<Scene>();
        XmlSerializer defaultSerializer;
        defaults->Serialize(defaultSerializer);
        const auto& defaultRoot = defaultSerializer.GetRootNode();
        for (auto attr = srcRoot.first_attribute(); attr; attr = attr.next_attribute()) {
            const bool alwaysWritten = StringView{attr.name()} == kNameAttribute;
            const pugi::xml_attribute defaultAttr = defaultRoot.attribute(attr.name());
            if (!alwaysWritten && defaultAttr && StringView{defaultAttr.value()} == StringView{attr.value()}) {
                continue;
            }
            sceneEl.append_attribute(attr.name()).set_value(attr.value());
        }
        for (auto child = srcRoot.first_child(); child; child = child.next_sibling()) {
            if (StringView{child.name()} != "nodes") {
                sceneEl.append_copy(child);
            }
        }

        pugi::xml_node nodesEl = sceneEl.append_child("nodes");
        for (const auto& nodePtr : scene.GetRootNode().GetChildren()) {
            if (!nodePtr->IsGenerated()) {
                AppendNode(nodesEl, *nodePtr.Get(), loader, notes);
            }
        }
        if (notes) {
            NoteOrphans(scene.GetRootNode(), false, *notes);
        }

        if (oldEl) {
            scenesEl.remove_child(oldEl);
        }
        return true;
    }

    std::expected<Vector<String>, StringView> SaveSceneToConfig(ConfigManager& configs, Scene& scene) {
        if (configs.IsBinaryConfig(ConfigNames::scene)) {
            return std::unexpected(StringView("SceneConfig is a cooked file, which is read only"));
        }
        XmlNode root = configs.GetConfig(ConfigNames::scene);
        const NodeFileLoader loader = MakeFileSystemNodeLoader(configs.GetFileSystem());
        Vector<String> notes;
        if (!root || !WriteSceneToConfig(root.GetPugiNode(), scene, &loader, &notes)) {
            return std::unexpected(StringView("there is no SceneConfig"));
        }
        for (const String& note : notes) {
            LOG_WARNING("Scene save: scene '{}': {}", scene.GetName().CStr(), note);
        }
        if (!configs.SaveConfig(ConfigNames::scene)) {
            return std::unexpected(StringView("SceneConfig could not be written, the log says why"));
        }
        return notes;
    }

}  // namespace BECore
