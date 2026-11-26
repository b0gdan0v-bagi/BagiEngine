#include "FieldTooltips.h"

#include <BECore/Reflection/TypeRegistry.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Scene/IComponent.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <Widgets/SceneEditor/FieldHelp.h>
#include <imgui.h>

namespace BECore {

    namespace {

        /// The widest a line of a tooltip gets, in font sizes
        constexpr float kWrapWidthInEm = 28.0f;

        void Append(String& text, StringView part) {
            text.append(part.data(), part.size());
        }

        /// The name of an opaque type as the compiler spells it, without its namespaces, its template arguments and a leading `struct`
        StringView ShortName(StringView name) {
            name = name.substr(0, name.find('<'));
            const size_t separator = name.find_last_of(": ");
            return separator == StringView::npos ? name : name.substr(separator + 1);
        }

        /// What a value of @p type is, in words: the kind, the class, or the elements of a container
        void AppendTypeWords(String& text, const TypeDesc& type) {
            const auto element = [&text](StringView lead, const TypeDesc* of) {
                Append(text, lead);
                if (of != nullptr) {
                    AppendTypeWords(text, *of);
                }
            };
            switch (type.kind) {
                case TypeKind::Object:
                case TypeKind::Pointer:
                    Append(text, type.ObjectName().empty() ? TypeKindName(TypeKind::Object) : type.ObjectName());
                    break;
                case TypeKind::Opaque:
                    Append(text, ShortName(type.opaqueName));
                    break;
                case TypeKind::List:
                    element("list of ", type.element);
                    break;
                case TypeKind::Map:
                    element("map of ", type.key);
                    element(" to ", type.element);
                    break;
                case TypeKind::Optional:
                    element("optional ", type.element);
                    break;
                case TypeKind::Value:
                    element("value of ", type.element);
                    break;
                case TypeKind::Pair:
                    element("pair of ", type.key);
                    element(" and ", type.element);
                    break;
                case TypeKind::Variant: {
                    StringView lead = "one of ";
                    for (const TypeDesc* alternative : type.alternatives) {
                        element(lead, alternative);
                        lead = ", ";
                    }
                    break;
                }
                default:
                    Append(text, TypeKindName(type.kind));
                    break;
            }
        }

    }  // namespace

    const char* FieldTooltips::Of(IComponent& component, StringView field) {
        if (_version != _help.GetVersion()) {
            _texts.clear();
            _version = _help.GetVersion();
        }
        HashMap<uint64_t, String>& fields = _texts[component.GetTypeMeta().typeHash];
        const uint64_t key = StringUtils::GetHash(field);
        auto found = fields.find(key);
        if (found == fields.end()) {
            found = fields.emplace(key, Compose(component, field)).first;
        }
        return found->second.c_str();
    }

    String FieldTooltips::Compose(IComponent& component, StringView field) const {
        const TypedRef live = TypedRef::Of(component);
        const FieldDesc* const desc = live ? FindReflectedField(*live.type, field) : nullptr;
        if (desc == nullptr) {
            return {};
        }
        String text;
        if (const FieldHelpAnswer help = _help.Find(live.type->Name(), field); !help.text.empty()) {
            Append(text, help.text);
            text += '\n';
        }
        AppendTypeWords(text, *desc->type);
        const FieldAttributes& attributes = desc->attributes;
        if (attributes.hasRange && attributes.rangeMin < attributes.rangeMax) {
            text += "\nrange {} .. {}"_format(attributes.rangeMin, attributes.rangeMax);
        }
        if (const Optional<StringView> value = _defaults.DefaultText(component, field)) {
            Append(text, "\ndefault: ");
            Append(text, *value);
        }
        return text;
    }

    void WrappedItemTooltip(const char* text) {
        if (text == nullptr || *text == '\0' || !ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) {
            return;
        }
        if (ImGui::BeginTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * kWrapWidthInEm);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

}  // namespace BECore
