#include "SceneNodeFiles.h"

#include <BECore/Config/ContentVersion.h>

namespace BECore {

    namespace {

        // Same form as the names in the header: string_view over a literal, so `.data()` is null-terminated for pugixml
        constexpr eastl::string_view kScenesElement = "scenes";
        constexpr eastl::string_view kSceneElement = "scene";
        constexpr eastl::string_view kNodesElement = "nodes";
        constexpr eastl::string_view kSceneNameAttribute = "name";
        constexpr char kRefSeparator = '/';

        bool IsRefChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        bool IsNodeOverrideAttribute(eastl::string_view name) {
            return std::find(kNodeOverrideAttributes.begin(), kNodeOverrideAttributes.end(), name) != kNodeOverrideAttributes.end();
        }

        /// One `<component>` of a reference and the component of the node file it is merged into
        struct ComponentOverride {
            pugi::xml_node source;
            pugi::xml_node target;
        };

        /// The `<component type="@p type">` of @p node, empty when there is none
        pugi::xml_node FindComponent(pugi::xml_node node, eastl::string_view type) {
            const pugi::xml_node components = node.child(kComponentsElementName.data());
            for (pugi::xml_node component = components.child(kComponentElementName.data()); component; component = component.next_sibling(kComponentElementName.data())) {
                if (eastl::string_view{component.attribute(kComponentTypeAttribute.data()).as_string()} == type) {
                    return component;
                }
            }
            return {};
        }

        /// A field written in both spellings in one override has no single winner; empty when fine, else the reason
        eastl::string CheckFieldSpelling(pugi::xml_node source, eastl::string_view type) {
            for (pugi::xml_attribute attr = source.first_attribute(); attr; attr = attr.next_attribute()) {
                if (eastl::string_view{attr.name()} != kComponentTypeAttribute && source.child(attr.name())) {
                    return Format("component '{}': field '{}' is written both as an attribute and as an element", type, attr.name());
                }
            }
            return {};
        }

        /// Checks the whole reference element and looks every component up in @p node, changing nothing
        bool CollectOverrides(pugi::xml_node node, pugi::xml_node reference, eastl::vector<ComponentOverride>& overrides, eastl::string& error) {
            for (pugi::xml_attribute attr = reference.first_attribute(); attr; attr = attr.next_attribute()) {
                const eastl::string_view name = attr.name();
                if (name != kNodeRefAttribute && !IsNodeOverrideAttribute(name)) {
                    error = Format("attribute '{}' is not supported on a node reference", name);
                    return false;
                }
            }

            pugi::xml_node componentsEl;
            for (pugi::xml_node child = reference.first_child(); child; child = child.next_sibling()) {
                if (child.type() != pugi::node_element) {
                    continue;
                }
                if (eastl::string_view{child.name()} != kComponentsElementName) {
                    error = Format("element '{}' is not supported inside a node reference", child.name());
                    return false;
                }
                if (componentsEl) {
                    error = Format("a node reference has one '{}' element at most", kComponentsElementName);
                    return false;
                }
                componentsEl = child;
            }

            for (pugi::xml_node source = componentsEl.first_child(); source; source = source.next_sibling()) {
                if (source.type() != pugi::node_element) {
                    continue;
                }
                if (eastl::string_view{source.name()} != kComponentElementName) {
                    error = Format("element '{}' is not supported inside '{}'", source.name(), kComponentsElementName);
                    return false;
                }
                const eastl::string_view type = source.attribute(kComponentTypeAttribute.data()).as_string();
                if (type.empty()) {
                    error = Format("a '{}' of a node reference has no '{}'", kComponentElementName, kComponentTypeAttribute);
                    return false;
                }
                for (const ComponentOverride& seen : overrides) {
                    if (eastl::string_view{seen.source.attribute(kComponentTypeAttribute.data()).as_string()} == type) {
                        error = Format("component '{}' is overridden twice", type);
                        return false;
                    }
                }
                const pugi::xml_node target = FindComponent(node, type);
                if (!target) {
                    error = Format("node file has no component '{}'", type);
                    return false;
                }
                error = CheckFieldSpelling(source, type);
                if (!error.empty()) {
                    return false;
                }
                overrides.push_back({source, target});
            }
            return true;
        }

        void SetAttribute(pugi::xml_node node, const char* name, const char* value) {
            pugi::xml_attribute attr = node.attribute(name);
            if (!attr) {
                attr = node.append_attribute(name);
            }
            attr.set_value(value);
        }

        /// Writes the override's fields over @p target; each field is replaced whole, in the spelling of the override
        void MergeComponent(pugi::xml_node target, pugi::xml_node source) {
            for (pugi::xml_attribute attr = source.first_attribute(); attr; attr = attr.next_attribute()) {
                if (eastl::string_view{attr.name()} == kComponentTypeAttribute) {
                    continue;
                }
                while (target.remove_child(attr.name())) {
                }
                SetAttribute(target, attr.name(), attr.value());
            }

            for (pugi::xml_node field = source.first_child(); field; field = field.next_sibling()) {
                // One pass per distinct name: a repeated element is copied together with its first one
                if (field.type() != pugi::node_element || field.previous_sibling(field.name())) {
                    continue;
                }
                target.remove_attribute(field.name());
                const pugi::xml_node first = target.child(field.name());
                for (pugi::xml_node copy = field; copy; copy = copy.next_sibling(field.name())) {
                    if (first) {
                        target.insert_copy_before(copy, first);
                    } else {
                        target.append_copy(copy);
                    }
                }
                for (pugi::xml_node old = first; old;) {
                    const pugi::xml_node next = old.next_sibling(field.name());
                    target.remove_child(old);
                    old = next;
                }
            }
        }

        /// Parses @p file into @p out and checks that its document element is a node that is not a reference itself and, when
        /// @p versionLabel is not empty, that its content version is the engine's (the label names the file in the message)
        bool ReadNodeFile(const std::filesystem::path& file, pugi::xml_document& out, eastl::string& error, eastl::string_view versionLabel = {}) {
            std::error_code ec;
            if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
                error = ec ? Format("the node file cannot be read: {}", ec.message()) : eastl::string{"the node file does not exist"};
                return false;
            }
            const pugi::xml_parse_result parsed = out.load_file(file.c_str());
            if (!parsed) {
                error = Format("the node file does not parse: {}", parsed.description());
                return false;
            }
            const pugi::xml_node root = out.document_element();
            if (eastl::string_view{root.name()} != kNodeElementName) {
                error = Format("the document element of the node file is '{}', not '{}'", root.name(), kNodeElementName);
                return false;
            }
            if (root.attribute(kNodeRefAttribute.data())) {
                error = "the node of a node file cannot be a reference itself";
                return false;
            }
            if (!versionLabel.empty()) {
                if (const auto version = CheckContentVersion(root, versionLabel); !version) {
                    error = version.error();
                    return false;
                }
            }
            return true;
        }

        std::filesystem::path FileOf(const std::filesystem::path& baseDirectory, eastl::string_view ref) {
            return baseDirectory / (std::filesystem::path(std::string_view(ref.data(), ref.size())) += kNodeFileExtension.data());
        }

        void Fail(NodeExpansion& result, eastl::string_view scene, eastl::string_view ref, eastl::string_view reason) {
            result.errors.push_back(Format("scene '{}': node reference '{}': {}", scene, ref, reason));
        }

        void ExpandScene(pugi::xml_node sceneEl, const NodeFileLoader& loader, bool markSource, NodeExpansion& result) {
            const eastl::string_view sceneName = sceneEl.attribute(kSceneNameAttribute.data()).as_string();

            for (pugi::xml_node nodesEl = sceneEl.child(kNodesElement.data()); nodesEl; nodesEl = nodesEl.next_sibling(kNodesElement.data())) {
                pugi::xml_node next;
                for (pugi::xml_node el = nodesEl.child(kNodeElementName.data()); el; el = next) {
                    next = el.next_sibling(kNodeElementName.data());

                    const pugi::xml_attribute refAttr = el.attribute(kNodeRefAttribute.data());
                    if (!refAttr) {
                        continue;
                    }

                    pugi::xml_document expanded;
                    eastl::string error;
                    if (ExpandNodeReference(el, loader, markSource, expanded, error)) {
                        nodesEl.insert_copy_before(expanded.document_element(), el);
                        ++result.expanded;
                    } else {
                        Fail(result, sceneName, refAttr.as_string(), error);
                    }
                    nodesEl.remove_child(el);
                }
            }
        }

    }  // namespace

    bool IsValidNodeRef(eastl::string_view ref) {
        if (ref.empty()) {
            return false;
        }
        size_t segmentLength = 0;
        for (const char c : ref) {
            if (c == kRefSeparator) {
                if (segmentLength == 0) {
                    return false;
                }
                segmentLength = 0;
            } else if (IsRefChar(c)) {
                ++segmentLength;
            } else {
                return false;
            }
        }
        return segmentLength != 0;
    }

    bool ApplyNodeOverrides(pugi::xml_node node, pugi::xml_node reference, eastl::string& error) {
        eastl::vector<ComponentOverride> overrides;
        if (!CollectOverrides(node, reference, overrides, error)) {
            return false;
        }

        for (const eastl::string_view name : kNodeOverrideAttributes) {
            if (const pugi::xml_attribute attr = reference.attribute(name.data())) {
                SetAttribute(node, name.data(), attr.value());
            }
        }
        for (const ComponentOverride& item : overrides) {
            MergeComponent(item.target, item.source);
        }
        return true;
    }

    bool ExpandNodeReference(pugi::xml_node reference, const NodeFileLoader& loader, bool markSource, pugi::xml_document& out, eastl::string& error) {
        out.reset();
        const eastl::string ref = reference.attribute(kNodeRefAttribute.data()).as_string();
        if (!IsValidNodeRef(ref)) {
            error = "not a valid reference (segments of letters, digits, '_' and '-' separated by '/')";
            return false;
        }

        pugi::xml_document file;
        if (!loader(ref, file, error)) {
            if (error.empty()) {
                error = "the node file cannot be loaded";
            }
            return false;
        }
        if (!ApplyNodeOverrides(file.document_element(), reference, error)) {
            return false;
        }

        // The version is a fact about the file, not about the node in the live scene
        pugi::xml_node node = out.append_copy(file.document_element());
        node.remove_attribute(kContentVersionAttribute.data());
        if (markSource) {
            SetAttribute(node, kNodeSourceAttribute.data(), ref.c_str());
        }
        return true;
    }

    NodeExpansion ExpandNodeReferences(pugi::xml_node configRoot, const NodeFileLoader& loader, bool markSource) {
        NodeExpansion result;
        for (pugi::xml_node scenesEl = configRoot.child(kScenesElement.data()); scenesEl; scenesEl = scenesEl.next_sibling(kScenesElement.data())) {
            for (pugi::xml_node sceneEl = scenesEl.child(kSceneElement.data()); sceneEl; sceneEl = sceneEl.next_sibling(kSceneElement.data())) {
                ExpandScene(sceneEl, loader, markSource, result);
            }
        }
        return result;
    }

    NodeFileLoader MakeDirectoryNodeLoader(std::filesystem::path baseDirectory) {
        return [baseDirectory = eastl::move(baseDirectory)](eastl::string_view ref, pugi::xml_document& out, eastl::string& error) {
            if (!IsValidNodeRef(ref)) {
                error = "not a valid reference";
                return false;
            }
            return ReadNodeFile(FileOf(baseDirectory, ref), out, error);
        };
    }

    NodeFileLoader MakeFileSystemNodeLoader(const FileSystem& fs) {
        return [&fs](eastl::string_view ref, pugi::xml_document& out, eastl::string& error) {
            if (!IsValidNodeRef(ref)) {
                error = "not a valid reference";
                return false;
            }
            const eastl::string virtualPath = Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeFileExtension);
            return ReadNodeFile(fs.ResolvePath(virtualPath), out, error, virtualPath);
        };
    }

    NodeExpansion LoadSceneDocument(const std::filesystem::path& sceneConfigFile, pugi::xml_document& out) {
        NodeExpansion result;
        out.reset();
        std::error_code ec;
        if (!std::filesystem::is_regular_file(sceneConfigFile, ec)) {
            result.errors.push_back(ec ? Format("scene file '{}' cannot be read: {}", sceneConfigFile.string(), ec.message()) : Format("scene file '{}' does not exist", sceneConfigFile.string()));
            return result;
        }
        const pugi::xml_parse_result parsed = out.load_file(sceneConfigFile.c_str());
        if (!parsed) {
            out.reset();
            result.errors.push_back(Format("scene file '{}' does not parse: {}", sceneConfigFile.string(), parsed.description()));
            return result;
        }
        return ExpandNodeReferences(out.document_element(), MakeDirectoryNodeLoader(sceneConfigFile.parent_path()));
    }

}  // namespace BECore
