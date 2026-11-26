#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class ComponentDefaults;
    class FieldHelp;
    class IComponent;
    struct FieldAttributes;
    struct TypeDesc;

    /// Appends what a value of @p type is, in words: the kind, the class, or the elements of a container
    void AppendTypeWords(String& text, const TypeDesc& type);

    /// The range of a field as `a .. b`; empty when it has none
    [[nodiscard]] String RangeWords(const FieldAttributes& attributes);

    /**
     * @brief The tooltips of the component fields: the description the help table answers, the type in words, the range and the default
     *
     * The text of a (component type, field) is composed at the first question and again only when the table's version moved, so a question asked
     * before allocates nothing.
     */
    class FieldTooltips {
    public:
        /// Both must outlive this object
        FieldTooltips(const FieldHelp& help, ComponentDefaults& defaults) : _help(help), _defaults(defaults) {}
        FieldTooltips(const FieldTooltips&) = delete;
        FieldTooltips& operator=(const FieldTooltips&) = delete;

        /// The tooltip of the top-level field of this serialized name, a line per part; empty for a name that is no field of the component's type.
        /// The text lives until a question that finds the table's version moved
        const char* Of(IComponent& component, StringView field);

    private:
        [[nodiscard]] String Compose(IComponent& component, StringView field) const;

        const FieldHelp& _help;
        ComponentDefaults& _defaults;
        /// By the hash of the component's type, then of the field's name
        HashMap<uint64_t, HashMap<uint64_t, String>> _texts;
        uint64_t _version = 0;
    };

    /// The tooltips of the fields of one component
    struct ComponentFieldHelp {
        FieldTooltips& tooltips;
        IComponent& component;

        [[nodiscard]] const char* Of(StringView field) const {
            return tooltips.Of(component, field);
        }
    };

    /// Shows @p text, wrapped to a readable width, while the last item is hovered, also when it is disabled (nothing for a null or empty text)
    void WrappedItemTooltip(const char* text);

}  // namespace BECore
