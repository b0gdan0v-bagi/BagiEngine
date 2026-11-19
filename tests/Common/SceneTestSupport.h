#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/Types/Types.h>
#include <pugixml.hpp>
#include <sstream>

namespace BECore::Tests {

    /// The names of the tree below @p root, depth first, each followed by one dot per level ("A B B1. B1a.. B2. C")
    [[nodiscard]] inline String TreeOf(const SceneNode& root) {
        String out;
        const auto append = [&out](const auto& self, const SceneNode& node, size_t depth) -> void {
            for (const auto& child : node.GetChildren()) {
                if (!out.empty()) {
                    out.append(" ");
                }
                out.append(child->GetName().CStr());
                out.append(depth, '.');
                self(self, *child, depth + 1);
            }
        };
        append(append, root, 0);
        return out;
    }

    /// The first node called @p name below @p from (depth first, @p from itself not included), or null; const when @p from is
    template <typename TNode>
    [[nodiscard]] TNode* FindNodeBelow(TNode& from, StringView name) {
        for (const auto& child : from.GetChildren()) {
            if (child->GetName().ToStringView() == name) {
                return child.Get();
            }
            if (TNode* found = FindNodeBelow<TNode>(*child, name)) {
                return found;
            }
        }
        return nullptr;
    }

    /// @p node printed on one line, no indentation: for comparing an element with the text it should have
    [[nodiscard]] inline String XmlText(const pugi::xml_node& node) {
        std::ostringstream out;
        node.print(out, "", pugi::format_raw);
        return FromStdString(out.str());
    }

    /// @p node printed as a file is saved, two spaces per level: for comparing whole documents and for readable failures
    [[nodiscard]] inline String XmlTextIndented(const pugi::xml_node& node) {
        std::ostringstream out;
        node.print(out, "  ", pugi::format_indent);
        return FromStdString(out.str());
    }

    /// The components of @p node as text, in order: "<type>:<the serialized element on one line>"
    [[nodiscard]] inline Vector<String> ComponentTexts(const SceneNode& node) {
        Vector<String> texts;
        for (const auto& component : node.GetComponents()) {
            XmlSerializer serializer;
            component->Serialize(serializer);
            texts.push_back(Format("{}:{}", component->GetTypeMeta().typeName, XmlText(serializer.GetRootNode())));
        }
        return texts;
    }

    /// The attributes of the scene `Main` that give it the view of a 800 x 600 window
    inline constexpr StringView kMainSceneView800x600 = R"(viewWidth="800" viewHeight="600")";

    /// A SceneConfig text with one scene, `Main`, holding @p nodesXml; @p sceneAttributes go into its `<scene>` element
    [[nodiscard]] inline String SceneConfigText(StringView nodesXml, StringView sceneAttributes = {}) {
        return Format(R"(<SceneManager><scenes><scene type="Scene" name="Main"{}{}><nodes>{}</nodes></scene></scenes></SceneManager>)", sceneAttributes.empty() ? "" : " ", sceneAttributes, nodesXml);
    }

}  // namespace BECore::Tests
