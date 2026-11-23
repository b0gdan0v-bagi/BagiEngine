#include "SceneNodeFiles.h"

#include <BECore/Config/ContentVersion.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/SchemaHash.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore {

    namespace {

        // Same form as the names in the header: string_view over a literal, so `.data()` is null-terminated for pugixml
        constexpr StringView kScenesElement = "scenes";
        constexpr StringView kSceneElement = "scene";
        constexpr StringView kNodesElement = "nodes";
        constexpr StringView kSceneNameAttribute = "name";
        constexpr char kRefSeparator = '/';

        bool IsRefChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        bool IsNodeOverrideAttribute(StringView name) {
            return std::find(kNodeOverrideAttributes.begin(), kNodeOverrideAttributes.end(), name) != kNodeOverrideAttributes.end();
        }

        /// One `<component>` of a reference and the component of the node file it is merged into
        struct ComponentOverride {
            pugi::xml_node source;
            pugi::xml_node target;
        };

        /// The `<component type="@p type">` of @p node, empty when there is none
        pugi::xml_node FindComponent(pugi::xml_node node, StringView type) {
            const pugi::xml_node components = node.child(kComponentsElementName.data());
            for (pugi::xml_node component = components.child(kComponentElementName.data()); component; component = component.next_sibling(kComponentElementName.data())) {
                if (StringView{component.attribute(kComponentTypeAttribute.data()).as_string()} == type) {
                    return component;
                }
            }
            return {};
        }

        /// A field written in both spellings in one override has no single winner; empty when fine, else the reason
        String CheckFieldSpelling(pugi::xml_node source, StringView type) {
            for (pugi::xml_attribute attr = source.first_attribute(); attr; attr = attr.next_attribute()) {
                if (StringView{attr.name()} != kComponentTypeAttribute && source.child(attr.name())) {
                    return Format("component '{}': field '{}' is written both as an attribute and as an element", type, attr.name());
                }
            }
            return {};
        }

        /// Checks the whole reference element and looks every component up in @p node, changing nothing
        bool CollectOverrides(pugi::xml_node node, pugi::xml_node reference, Vector<ComponentOverride>& overrides, String& error) {
            for (pugi::xml_attribute attr = reference.first_attribute(); attr; attr = attr.next_attribute()) {
                const StringView name = attr.name();
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
                if (StringView{child.name()} != kComponentsElementName) {
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
                if (StringView{source.name()} != kComponentElementName) {
                    error = Format("element '{}' is not supported inside '{}'", source.name(), kComponentsElementName);
                    return false;
                }
                const StringView type = source.attribute(kComponentTypeAttribute.data()).as_string();
                if (type.empty()) {
                    error = Format("a '{}' of a node reference has no '{}'", kComponentElementName, kComponentTypeAttribute);
                    return false;
                }
                for (const ComponentOverride& seen : overrides) {
                    if (StringView{seen.source.attribute(kComponentTypeAttribute.data()).as_string()} == type) {
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
                if (StringView{attr.name()} == kComponentTypeAttribute) {
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
        bool ReadNodeFile(const std::filesystem::path& file, pugi::xml_document& out, String& error, StringView versionLabel = {}) {
            std::error_code ec;
            if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
                error = ec ? Format("the node file cannot be read: {}", ec.message()) : String{"the node file does not exist"};
                return false;
            }
            const pugi::xml_parse_result parsed = out.load_file(file.c_str());
            if (!parsed) {
                error = Format("the node file does not parse: {}", parsed.description());
                return false;
            }
            const pugi::xml_node root = out.document_element();
            if (StringView{root.name()} != kNodeElementName) {
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

        /// Reads the cooked node file @p file into @p out as the `<node>` element of the same node, so the overrides and the
        /// instantiation work on it as on a file written by hand. @p label names the file in the messages
        bool ReadNodeFileBinary(const std::filesystem::path& file, StringView label, pugi::xml_document& out, String& error) {
            BinaryDeserializer reader;
            if (!reader.LoadFromFile(file)) {
                const auto& errors = reader.GetErrors();
                error = Format("the node file '{}' is not a valid binary document: {}", label, errors.empty() ? StringView{"it cannot be read"} : StringView{errors.front().errorMessage});
                return false;
            }
            const auto stamp = CheckContentStamp(reader, label);
            if (!stamp) {
                error = stamp.error();
                return false;
            }
            if (stamp->rootType != kNodeRootType) {
                error = Format("the node file '{}' holds a '{}', not a '{}'", label, stamp->rootType, kNodeRootType);
                return false;
            }
            out.reset();
            AppendNodeElement(out, *DeserializeNode(reader, label));
            return true;
        }

        /// What of a node file one layer holds: the path of its `.xml`, of its `.bin`, none or both
        struct NodeFileInLayer {
            std::filesystem::path xml;
            std::filesystem::path binary;
        };

        NodeFileInLayer FindInLayer(const std::filesystem::path& layer, StringView ref) {
            const std::filesystem::path stem = layer / std::filesystem::path(ToStd(ref));
            NodeFileInLayer found;
            std::error_code ec;
            const std::filesystem::path xml = std::filesystem::path(stem) += kNodeFileExtension.data();
            const std::filesystem::path binary = std::filesystem::path(stem) += kNodeBinaryExtension.data();
            if (std::filesystem::is_regular_file(xml, ec)) {
                found.xml = xml;
            }
            if (std::filesystem::is_regular_file(binary, ec)) {
                found.binary = binary;
            }
            return found;
        }

        std::filesystem::path FileOf(const std::filesystem::path& baseDirectory, StringView ref) {
            return baseDirectory / (std::filesystem::path(ToStd(ref)) += kNodeFileExtension.data());
        }

        /// What one call of ExpandNodeReferences learned of one node file: the parsed document or why it failed. The
        /// document is read only (an instance copies its node), so references to one file share it
        struct CachedNodeFile {
            pugi::xml_document document;
            String error;  // empty when the file loaded
        };

        using NodeFileCache = HashMap<String, CachedNodeFile>;

        /// The file of @p ref as @p loader gives it, asked once per cache; the failure reason is kept as well
        const CachedNodeFile& LoadCached(NodeFileCache& cache, StringView ref, NodeFileLoaderRef loader) {
            if (const auto found = cache.find(ref); found != cache.end()) {
                return found->second;
            }
            CachedNodeFile& entry = cache[String{ref}];
            if (!loader(ref, entry.document, entry.error)) {
                if (entry.error.empty()) {
                    entry.error = "the node file cannot be loaded";
                }
            } else {
                entry.error.clear();
            }
            return entry;
        }

        /// The one implementation of a reference: puts a copy of the cached file's `<node>` into @p parent (before @p before,
        /// at the end when that is empty) and applies the overrides to the copy. On false nothing of it is left in @p parent.
        /// The copy goes straight into the document that keeps it: a `xml_document` of its own per reference would cost a page
        bool InsertInstance(pugi::xml_node reference, NodeFileCache& cache, NodeFileLoaderRef loader, bool markSource, pugi::xml_node parent, pugi::xml_node before, String& error) {
            const char* ref = reference.attribute(kNodeRefAttribute.data()).as_string();
            if (!IsValidNodeRef(ref)) {
                error = "not a valid reference (segments of letters, digits, '_' and '-' separated by '/')";
                return false;
            }

            const CachedNodeFile& file = LoadCached(cache, ref, loader);
            if (!file.error.empty()) {
                error = file.error;
                return false;
            }

            const pugi::xml_node source = file.document.document_element();
            pugi::xml_node node = before ? parent.insert_copy_before(source, before) : parent.append_copy(source);
            // The version is a fact about the file, not about the node in the live scene
            node.remove_attribute(kContentVersionAttribute.data());
            if (!ApplyNodeOverrides(node, reference, error)) {
                parent.remove_child(node);
                return false;
            }
            if (markSource) {
                SetAttribute(node, kNodeSourceAttribute.data(), ref);
            }
            return true;
        }

        void Fail(NodeExpansion& result, StringView scene, StringView ref, StringView reason) {
            result.errors.push_back(Format("scene '{}': node reference '{}': {}", scene, ref, reason));
        }

        void ExpandScene(pugi::xml_node sceneEl, NodeFileCache& cache, NodeFileLoaderRef loader, bool markSource, NodeExpansion& result) {
            const StringView sceneName = sceneEl.attribute(kSceneNameAttribute.data()).as_string();

            for (pugi::xml_node nodesEl = sceneEl.child(kNodesElement.data()); nodesEl; nodesEl = nodesEl.next_sibling(kNodesElement.data())) {
                pugi::xml_node next;
                for (pugi::xml_node el = nodesEl.child(kNodeElementName.data()); el; el = next) {
                    next = el.next_sibling(kNodeElementName.data());

                    const pugi::xml_attribute refAttr = el.attribute(kNodeRefAttribute.data());
                    if (!refAttr) {
                        continue;
                    }

                    String error;
                    if (InsertInstance(el, cache, loader, markSource, nodesEl, el, error)) {
                        ++result.expanded;
                    } else {
                        Fail(result, sceneName, refAttr.as_string(), error);
                    }
                    nodesEl.remove_child(el);
                }
            }
        }

    }  // namespace

    bool IsValidNodeRef(StringView ref) {
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

    bool ApplyNodeOverrides(pugi::xml_node node, pugi::xml_node reference, String& error) {
        Vector<ComponentOverride> overrides;
        if (!CollectOverrides(node, reference, overrides, error)) {
            return false;
        }

        for (const StringView name : kNodeOverrideAttributes) {
            if (const pugi::xml_attribute attr = reference.attribute(name.data())) {
                SetAttribute(node, name.data(), attr.value());
            }
        }
        for (const ComponentOverride& item : overrides) {
            MergeComponent(item.target, item.source);
        }
        return true;
    }

    bool ExpandNodeReference(pugi::xml_node reference, NodeFileLoaderRef loader, bool markSource, pugi::xml_document& out, String& error) {
        out.reset();
        NodeFileCache cache;
        return InsertInstance(reference, cache, loader, markSource, out, {}, error);
    }

    NodeExpansion ExpandNodeReferences(pugi::xml_node configRoot, NodeFileLoaderRef loader, bool markSource) {
        NodeExpansion result;
        NodeFileCache cache;
        for (pugi::xml_node scenesEl = configRoot.child(kScenesElement.data()); scenesEl; scenesEl = scenesEl.next_sibling(kScenesElement.data())) {
            for (pugi::xml_node sceneEl = scenesEl.child(kSceneElement.data()); sceneEl; sceneEl = sceneEl.next_sibling(kSceneElement.data())) {
                ExpandScene(sceneEl, cache, loader, markSource, result);
            }
        }
        return result;
    }

    NodeFileLoader MakeDirectoryNodeLoader(std::filesystem::path baseDirectory) {
        return [baseDirectory = eastl::move(baseDirectory)](StringView ref, pugi::xml_document& out, String& error) {
            if (!IsValidNodeRef(ref)) {
                error = "not a valid reference";
                return false;
            }
            return ReadNodeFile(FileOf(baseDirectory, ref), out, error);
        };
    }

    NodeFileLoader MakeFileSystemNodeLoader(const FileSystem& fs) {
        return [&fs](StringView ref, pugi::xml_document& out, String& error) {
            if (!IsValidNodeRef(ref)) {
                error = "not a valid reference";
                return false;
            }
            const Vector<std::filesystem::path> layers = fs.GetMountLayers(MountPoints::config);
            for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
                const NodeFileInLayer found = FindInLayer(*layer, ref);
                if (!found.xml.empty() && !found.binary.empty()) {
                    error = Format("'{}' and '{}' are both node files of one reference: keep one", found.xml.string(), found.binary.string());
                    return false;
                }
                if (!found.binary.empty()) {
                    return ReadNodeFileBinary(found.binary, Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeBinaryExtension), out, error);
                }
                if (!found.xml.empty()) {
                    const String virtualPath = Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeFileExtension);
                    return ReadNodeFile(found.xml, out, error, virtualPath);
                }
            }
            // No layer has it: what a mount that is not a layer of `config` resolves to, and the usual "does not exist"
            const String virtualPath = Format("{}/{}{}", kNodeFilesMountPoint, ref, kNodeFileExtension);
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
