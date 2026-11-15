#pragma once

#include <BECore/Rules/TagTree.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>
#include <expected>
#include <initializer_list>

namespace BECore {

    /// What a RuleSelector matches: every value, one value, or the values that carry a tag of a range.
    CORE_ENUM(RuleSelectorKind, uint8_t, Any, Id, Tags)

    /// The specificity of an Id selector, above every tag selector (a tag of depth d gives at most 2 * d + 1).
    inline constexpr uint32_t kIdSpecificity = 2 * static_cast<uint32_t>(kMaxTagCount) + 2;

    /**
     * @brief What a rule says about one axis of a table: `Any`, `Id(value)` or `Tags(range)`; it matches a value when it is `Any`,
     * equals the id, or any tag of the value lies in the range.
     *
     * The specificity (`*` 0, `tag:p` 2 * Depth(p), `tag:p.*` 2 * Depth(p) + 1, an id kIdSpecificity) is decided when the
     * selector is made, so a selector built by `RuleAxis::ParseSelector` is what the baker compares.
     */
    class RuleSelector {
    public:
        [[nodiscard]] static constexpr RuleSelector Any() {
            return RuleSelector(RuleSelectorKind::Any, 0, TagRange{}, 0);
        }

        [[nodiscard]] static constexpr RuleSelector Id(uint32_t value) {
            return RuleSelector(RuleSelectorKind::Id, value, TagRange{}, kIdSpecificity);
        }

        /// @p specificity is 2 * depth of the tag, plus one when the range leaves out the tag itself.
        [[nodiscard]] static constexpr RuleSelector Tags(TagRange range, uint32_t specificity) {
            return RuleSelector(RuleSelectorKind::Tags, 0, range, specificity);
        }

        [[nodiscard]] constexpr RuleSelectorKind Kind() const {
            return _kind;
        }

        /// The value of an Id selector.
        [[nodiscard]] constexpr uint32_t Value() const {
            return _value;
        }

        /// The range of a Tags selector.
        [[nodiscard]] constexpr TagRange Range() const {
            return _range;
        }

        [[nodiscard]] constexpr uint32_t Specificity() const {
            return _specificity;
        }

        constexpr bool operator==(const RuleSelector&) const = default;

    private:
        constexpr RuleSelector(RuleSelectorKind kind, uint32_t value, TagRange range, uint32_t specificity) : _kind(kind), _value(value), _range(range), _specificity(specificity) {}

        RuleSelectorKind _kind = RuleSelectorKind::Any;
        uint32_t _value = 0;
        TagRange _range;
        uint32_t _specificity = 0;
    };

    /**
     * @brief One dimension of a rule table: a name, `Count()` values with names, and per value a set of tags of a baked TagTree
     * (a copy the axis owns). A value type.
     *
     * The text of a selector (`ParseSelector`): `*`; a value name; `tag:<path>` (the tag and its subtree); `tag:<path>.*` (its
     * descendants only). Surrounding spaces are trimmed.
     */
    class RuleAxis {
    public:
        /// An axis of named values with no tags; @p valueNames must be unique and not empty.
        [[nodiscard]] static std::expected<RuleAxis, String> Named(StringView name, Span<const StringView> valueNames);
        [[nodiscard]] static std::expected<RuleAxis, String> Named(StringView name, std::initializer_list<StringView> valueNames);

        /// As above, and the values may carry the tags of @p tags (a baked tree, copied); the tags are set with SetTags.
        [[nodiscard]] static std::expected<RuleAxis, String> Named(StringView name, Span<const StringView> valueNames, const TagTree& tags);
        [[nodiscard]] static std::expected<RuleAxis, String> Named(StringView name, std::initializer_list<StringView> valueNames, const TagTree& tags);

        /// An axis whose values are the tags of the baked @p tags: value `i` is tag `i`, its tag set is `{i}`, its name is the full path.
        [[nodiscard]] static RuleAxis OfTags(StringView name, const TagTree& tags);

        /// Gives the value the tags of these paths (replacing its earlier set); an unknown value or tag is an error.
        [[nodiscard]] std::expected<void, String> SetTags(uint32_t value, Span<const StringView> tagPaths);
        [[nodiscard]] std::expected<void, String> SetTags(StringView valueName, Span<const StringView> tagPaths);
        [[nodiscard]] std::expected<void, String> SetTags(uint32_t value, std::initializer_list<StringView> tagPaths);
        [[nodiscard]] std::expected<void, String> SetTags(StringView valueName, std::initializer_list<StringView> tagPaths);

        [[nodiscard]] StringView Name() const {
            return _name;
        }

        [[nodiscard]] uint32_t Count() const {
            return static_cast<uint32_t>(_valueNames.size());
        }

        [[nodiscard]] StringView ValueName(uint32_t value) const;

        [[nodiscard]] Optional<uint32_t> FindValue(StringView valueName) const;

        [[nodiscard]] const TagTree& Tags() const {
            return _tags;
        }

        /// The tags of a value, ascending.
        [[nodiscard]] Span<const TagId> ValueTags(uint32_t value) const;

        [[nodiscard]] std::expected<RuleSelector, String> ParseSelector(StringView text) const;

        [[nodiscard]] bool Matches(const RuleSelector& selector, uint32_t value) const;

        /// The values the selector matches, ascending.
        [[nodiscard]] Vector<uint32_t> Expand(const RuleSelector& selector) const;

    private:
        RuleAxis() = default;

        String _name;
        Vector<String> _valueNames;
        HashMap<String, uint32_t> _valueIndex;
        TagTree _tags;
        Vector<Vector<TagId>> _valueTags;
    };

}  // namespace BECore
