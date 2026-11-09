#include "SceneNodeFiles.h"

namespace BECore {

    namespace {

        constexpr const char* kScenesElement = "scenes";
        constexpr const char* kSceneElement = "scene";
        constexpr const char* kNodesElement = "nodes";
        constexpr const char* kSceneNameAttribute = "name";
        constexpr char kRefSeparator = '/';

        bool IsRefChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        }

        /// A reference element carries `ref` and nothing else; empty when it is fine, else the reason
        eastl::string CheckReferenceElement(pugi::xml_node reference) {
            for (pugi::xml_attribute attr = reference.first_attribute(); attr; attr = attr.next_attribute()) {
                if (eastl::string_view{attr.name()} != kNodeRefAttribute) {
                    return Format("attribute '{}' is not supported on a node reference", attr.name());
                }
            }
            for (pugi::xml_node child = reference.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_element) {
                    return Format("element '{}' is not supported inside a node reference", child.name());
                }
            }
            return {};
        }

        /// Parses @p file into @p out and checks that its document element is a node that is not a reference itself
        bool ReadNodeFile(const std::filesystem::path& file, pugi::xml_document& out, eastl::string& error) {
            if (file.empty() || !std::filesystem::is_regular_file(file)) {
                error = "the node file does not exist";
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
            return true;
        }

        std::filesystem::path FileOf(const std::filesystem::path& baseDirectory, eastl::string_view ref) {
            return baseDirectory / (std::filesystem::path(std::string_view(ref.data(), ref.size())) += kNodeFileExtension.data());
        }

        void Fail(NodeExpansion& result, eastl::string_view scene, eastl::string_view ref, eastl::string_view reason) {
            result.errors.push_back(Format("scene '{}': node reference '{}': {}", scene, ref, reason));
        }

        void ExpandScene(pugi::xml_node sceneEl, const NodeFileLoader& loader, NodeExpansion& result) {
            const eastl::string_view sceneName = sceneEl.attribute(kSceneNameAttribute).as_string();

            for (pugi::xml_node nodesEl = sceneEl.child(kNodesElement); nodesEl; nodesEl = nodesEl.next_sibling(kNodesElement)) {
                pugi::xml_node next;
                for (pugi::xml_node el = nodesEl.child(kNodeElementName.data()); el; el = next) {
                    next = el.next_sibling(kNodeElementName.data());

                    const pugi::xml_attribute refAttr = el.attribute(kNodeRefAttribute.data());
                    if (!refAttr) {
                        continue;
                    }
                    const eastl::string ref = refAttr.as_string();

                    eastl::string error = CheckReferenceElement(el);
                    if (error.empty() && !IsValidNodeRef(ref)) {
                        error = "not a valid reference (segments of letters, digits, '_' and '-' separated by '/')";
                    }

                    if (error.empty()) {
                        pugi::xml_document file;
                        if (loader(ref, file, error)) {
                            nodesEl.insert_copy_before(file.document_element(), el);
                            ++result.expanded;
                        } else if (error.empty()) {
                            error = "the node file cannot be loaded";
                        }
                    }

                    if (!error.empty()) {
                        Fail(result, sceneName, ref, error);
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

    NodeExpansion ExpandNodeReferences(pugi::xml_node configRoot, const NodeFileLoader& loader) {
        NodeExpansion result;
        for (pugi::xml_node scenesEl = configRoot.child(kScenesElement); scenesEl; scenesEl = scenesEl.next_sibling(kScenesElement)) {
            for (pugi::xml_node sceneEl = scenesEl.child(kSceneElement); sceneEl; sceneEl = sceneEl.next_sibling(kSceneElement)) {
                ExpandScene(sceneEl, loader, result);
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
            return ReadNodeFile(fs.ResolvePath(virtualPath), out, error);
        };
    }

    NodeExpansion LoadSceneDocument(const std::filesystem::path& sceneConfigFile, pugi::xml_document& out) {
        NodeExpansion result;
        out.reset();
        if (!std::filesystem::is_regular_file(sceneConfigFile)) {
            result.errors.push_back(Format("scene file '{}' does not exist", sceneConfigFile.string()));
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
