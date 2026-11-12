#include "FuzzTargets.h"

#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneNodeInstance.h>

namespace BECore::Fuzz {

    namespace {

        /// The reference every node file of the target is found under
        constexpr eastl::string_view kNodeRef = "nodes/Fuzz";

        /// The scene an input without a NUL byte is a node file for: one reference, no override
        constexpr const char* kReferenceScene = R"(<SceneManager><scenes><scene type="Scene" name="Fuzz"><nodes><node ref="nodes/Fuzz"/></nodes></scene></scenes></SceneManager>)";

        /// Whether @p text is a node file the loader accepts: parses, and its document element is a `node` that is no reference
        bool IsNodeFile(const pugi::xml_document& document) {
            const pugi::xml_node root = document.document_element();
            return root && eastl::string_view(root.name()) == kNodeElementName && !root.attribute(kNodeRefAttribute.data());
        }

        /// A loader that serves the node file of the input under kNodeRef and nothing else
        NodeFileLoader MakeInputLoader(eastl::string_view nodeText) {
            return [nodeText](eastl::string_view ref, pugi::xml_document& out, eastl::string& error) {
                if (ref != kNodeRef) {
                    error = "no such node file";
                    return false;
                }
                if (!out.load_buffer(nodeText.data(), nodeText.size()) || !IsNodeFile(out)) {
                    error = "not a node file";
                    return false;
                }
                return true;
            };
        }

        /// The `<node ref>` elements the expansion looks at: those of every `nodes` of every `scene` of every `scenes` of the root
        size_t CountReferences(pugi::xml_node root) {
            size_t count = 0;
            for (const pugi::xml_node scenes : root.children("scenes")) {
                for (const pugi::xml_node scene : scenes.children("scene")) {
                    for (const pugi::xml_node nodes : scene.children("nodes")) {
                        for (const pugi::xml_node node : nodes.children("node")) {
                            if (node.attribute(kNodeRefAttribute.data())) {
                                ++count;
                            }
                        }
                    }
                }
            }
            return count;
        }

    }  // namespace

    void FuzzSceneNode(const uint8_t* data, size_t size) {
        const auto* begin = reinterpret_cast<const char*>(data);
        const char* separator = size == 0 ? nullptr : static_cast<const char*>(std::memchr(begin, '\0', size));
        // Each text in a buffer of its own, as the loaders have it (a file is read into one): pugixml reads a text that looks like UTF-16 or UTF-32 as words
        eastl::string sceneText(kReferenceScene);
        eastl::string nodeText(begin, size);
        if (separator != nullptr) {
            const auto sceneSize = static_cast<size_t>(separator - begin);
            sceneText.assign(begin, sceneSize);
            nodeText.assign(separator + 1, size - sceneSize - 1);
        }

        pugi::xml_document sceneDocument;
        if (!sceneDocument.load_buffer(sceneText.data(), sceneText.size()) || !sceneDocument.document_element()) {
            return;
        }
        const NodeFileLoader loader = MakeInputLoader(eastl::string_view(nodeText.data(), nodeText.size()));

        // The expansion alone is pure pugixml: every reference is replaced or reported, and what is left has none
        pugi::xml_document expanded;
        expanded.append_copy(sceneDocument.document_element());
        const size_t references = CountReferences(expanded.document_element());
        const NodeExpansion expansion = ExpandNodeReferences(expanded.document_element(), loader, true);
        FUZZ_CHECK(expansion.expanded + expansion.errors.size() == references);
        FUZZ_CHECK(CountReferences(expanded.document_element()) == 0);
        FUZZ_CHECK(ExpandNodeReferences(expanded.document_element(), loader, true).expanded == 0);

        // The components of the expanded scene deserialized, as the loader does
        const IntrusivePtrAtomic<SceneManager> manager = LoadSceneManager(XmlNode{sceneDocument.document_element()}, loader);
        FUZZ_CHECK(manager);
    }

}  // namespace BECore::Fuzz
