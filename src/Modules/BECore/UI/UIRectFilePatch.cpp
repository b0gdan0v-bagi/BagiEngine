#include "UIRectFilePatch.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIElementStyle.h>

namespace BECore {

    namespace {

        constexpr StringView kNodeComponentsName = "components";
        constexpr StringView kNodeComponentName = "component";
        constexpr StringView kComponentTypeName = "type";
        constexpr StringView kCanvasTypeName = "UICanvasComponent";
        constexpr StringView kElementsName = "elements";
        constexpr StringView kChildrenName = "children";
        constexpr StringView kElementName = "element";
        constexpr StringView kNameAttribute = "name";
        constexpr const char* kScratchName = "e";

        bool FindPath(const UIElement& target, const Vector<IntrusivePtrNonAtomic<UIElement>>& list, Vector<uint32_t>& path) {
            for (size_t i = 0; i < list.size(); ++i) {
                path.push_back(static_cast<uint32_t>(i));
                if (list[i].Get() == &target || FindPath(target, list[i]->children, path)) {
                    return true;
                }
                path.pop_back();
            }
            return false;
        }

        /// The @p ordinal-th child element of @p parent called @p name (and, when @p typeValue is given, with that `type` attribute)
        pugi::xml_node NthChild(pugi::xml_node parent, StringView name, uint32_t ordinal, StringView typeValue = {}) {
            uint32_t seen = 0;
            for (pugi::xml_node child : parent.children()) {
                if (child.type() != pugi::node_element || StringView{child.name()} != name) {
                    continue;
                }
                if (!typeValue.empty() && StringView{child.attribute(String(kComponentTypeName).c_str()).value()} != typeValue) {
                    continue;
                }
                if (seen++ == ordinal) {
                    return child;
                }
            }
            return {};
        }

        struct RectAttribute {
            String name;
            String value;
        };

        /// The rect attributes of @p rect as UIElementStyle::Serialize writes them: name and text, in the writer's order
        Vector<RectAttribute> RectAttributes(const UIRectSpec& rect) {
            UIElementStyle style;
            style.rect = rect;
            XmlSerializer serializer;
            serializer.BeginObject(kScratchName);
            style.Serialize(serializer);
            serializer.EndObject();

            Vector<RectAttribute> attributes;
            for (pugi::xml_attribute attribute : serializer.GetRootNode().child(kScratchName).attributes()) {
                attributes.push_back({String(attribute.name()), String(attribute.value())});
            }
            return attributes;
        }

        UIRectSpec ReadRect(pugi::xml_node element) {
            XmlDeserializer deserializer;
            UIElementStyle style;
            if (deserializer.LoadFromXmlNode(XmlNode{element})) {
                style.Deserialize(deserializer);
            }
            return style.rect;
        }

        const String* FindValue(const Vector<RectAttribute>& attributes, const String& name) {
            for (const auto& attribute : attributes) {
                if (attribute.name == name) {
                    return &attribute.value;
                }
            }
            return nullptr;
        }

    }  // namespace

    Optional<Vector<uint32_t>> UIRectFilePatch::FindAuthoredPath(const UICanvasComponent& canvas, const UIElement& element) {
        Vector<uint32_t> path;
        if (FindPath(element, canvas._elements, path)) {
            return path;
        }
        return std::nullopt;
    }

    pugi::xml_node UIRectFilePatch::FindFileElement(pugi::xml_node nodeEl, uint32_t canvasOrdinal, Span<const uint32_t> path, const UIElement& live, String& why) {
        if (path.empty()) {
            why = "no path";
            return {};
        }
        const pugi::xml_node canvasEl = NthChild(nodeEl.child(String(kNodeComponentsName).c_str()), kNodeComponentName, canvasOrdinal, kCanvasTypeName);
        if (!canvasEl) {
            why = Format("the file has no canvas number {}", canvasOrdinal);
            return {};
        }

        pugi::xml_node list = canvasEl.child(String(kElementsName).c_str());
        pugi::xml_node element;
        for (size_t depth = 0; depth < path.size(); ++depth) {
            element = NthChild(list, kElementName, path[depth]);
            if (!element) {
                why = Format("the file has no element number {} at depth {}", path[depth], depth);
                return {};
            }
            list = element.child(String(kChildrenName).c_str());
        }

        const StringView fileType = element.attribute(String(kComponentTypeName).c_str()).value();
        const StringView liveType = live.GetTypeMeta().typeName;
        if (fileType != liveType) {
            why = Format("the file's element is a '{}', the scene's a '{}'", fileType, liveType);
            return {};
        }
        const StringView fileName = element.attribute(String(kNameAttribute).c_str()).value();
        if (fileName != live.name.ToStringView()) {
            why = Format("the file's element is called '{}', the scene's '{}'", fileName, live.name.ToStringView());
            return {};
        }
        return element;
    }

    Vector<XmlTextPatch::AttributeEdit> UIRectFilePatch::RectAttributeEdits(pugi::xml_node fileElement, const UIRectSpec& live) {
        const Vector<RectAttribute> fileAttributes = RectAttributes(ReadRect(fileElement));
        const Vector<RectAttribute> liveAttributes = RectAttributes(live);

        Vector<XmlTextPatch::AttributeEdit> edits;
        for (const auto& [name, value] : liveAttributes) {
            const String* fileValue = FindValue(fileAttributes, name);
            if (fileValue == nullptr || *fileValue != value) {
                edits.push_back({name, value});
            }
        }
        for (const auto& [name, value] : fileAttributes) {
            if (FindValue(liveAttributes, name) == nullptr) {
                edits.push_back({name, std::nullopt});
            }
        }
        return edits;
    }

}  // namespace BECore
