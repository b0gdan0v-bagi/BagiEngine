#include "RuleAxis.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kTagPrefix = "tag:";
        constexpr StringView kDescendantsSuffix = ".*";
        constexpr StringView kAnyText = "*";

        bool IsSpace(char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        }

        StringView Trim(StringView text) {
            while (!text.empty() && IsSpace(text.front())) {
                text.remove_prefix(1);
            }
            while (!text.empty() && IsSpace(text.back())) {
                text.remove_suffix(1);
            }
            return text;
        }

        bool StartsWith(StringView text, StringView prefix) {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool EndsWith(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

        /// A tree that is empty and baked, for an axis without tags.
        TagTree EmptyBakedTree() {
            TagTree tree;
            tree.Bake();
            return tree;
        }

    }  // namespace

    std::expected<RuleAxis, String> RuleAxis::Named(StringView name, Span<const StringView> valueNames) {
        return Named(name, valueNames, EmptyBakedTree());
    }

    std::expected<RuleAxis, String> RuleAxis::Named(StringView name, std::initializer_list<StringView> valueNames) {
        return Named(name, Span<const StringView>(valueNames.begin(), valueNames.size()), EmptyBakedTree());
    }

    std::expected<RuleAxis, String> RuleAxis::Named(StringView name, std::initializer_list<StringView> valueNames, const TagTree& tags) {
        return Named(name, Span<const StringView>(valueNames.begin(), valueNames.size()), tags);
    }

    std::expected<RuleAxis, String> RuleAxis::Named(StringView name, Span<const StringView> valueNames, const TagTree& tags) {
        ASSERT(tags.IsBaked(), "RuleAxis::Named: the tag tree must be baked");
        RuleAxis axis;
        axis._name = String(name);
        axis._tags = tags;
        for (StringView valueName : valueNames) {
            if (valueName.empty() || valueName == kAnyText || StartsWith(valueName, kTagPrefix) || valueName != Trim(valueName)) {
                return std::unexpected(Format("axis '{}': '{}' cannot be a value name (empty, '*', starting with 'tag:' or with a space at an end)", name, valueName));
            }
            const uint32_t value = static_cast<uint32_t>(axis._valueNames.size());
            if (!axis._valueIndex.emplace(String(valueName), value).second) {
                return std::unexpected(Format("axis '{}': the value name '{}' is used twice", name, valueName));
            }
            axis._valueNames.push_back(String(valueName));
        }
        axis._valueTags.resize(axis._valueNames.size());
        return axis;
    }

    RuleAxis RuleAxis::OfTags(StringView name, const TagTree& tags) {
        ASSERT(tags.IsBaked(), "RuleAxis::OfTags: the tag tree must be baked");
        RuleAxis axis;
        axis._name = String(name);
        axis._tags = tags;
        for (size_t i = 0; i < tags.Count(); ++i) {
            const TagId tag = static_cast<TagId>(i);
            axis._valueIndex.emplace(String(tags.Name(tag)), static_cast<uint32_t>(i));
            axis._valueNames.push_back(String(tags.Name(tag)));
            axis._valueTags.push_back(Vector<TagId>{tag});
        }
        return axis;
    }

    std::expected<void, String> RuleAxis::SetTags(uint32_t value, Span<const StringView> tagPaths) {
        if (value >= Count()) {
            return std::unexpected(Format("axis '{}': value {} is out of range (the axis has {} values)", _name, value, Count()));
        }
        Vector<TagId> tags;
        for (StringView path : tagPaths) {
            const Optional<TagId> tag = _tags.Find(path);
            if (!tag) {
                return std::unexpected(Format("axis '{}': value '{}' names the unknown tag '{}'", _name, _valueNames[value], path));
            }
            tags.push_back(*tag);
        }
        eastl::sort(tags.begin(), tags.end());
        tags.erase(eastl::unique(tags.begin(), tags.end()), tags.end());
        _valueTags[value] = std::move(tags);
        return {};
    }

    std::expected<void, String> RuleAxis::SetTags(StringView valueName, Span<const StringView> tagPaths) {
        const Optional<uint32_t> value = FindValue(valueName);
        if (!value) {
            return std::unexpected(Format("axis '{}': there is no value '{}' to set tags on", _name, valueName));
        }
        return SetTags(*value, tagPaths);
    }

    std::expected<void, String> RuleAxis::SetTags(uint32_t value, std::initializer_list<StringView> tagPaths) {
        return SetTags(value, Span<const StringView>(tagPaths.begin(), tagPaths.size()));
    }

    std::expected<void, String> RuleAxis::SetTags(StringView valueName, std::initializer_list<StringView> tagPaths) {
        return SetTags(valueName, Span<const StringView>(tagPaths.begin(), tagPaths.size()));
    }

    StringView RuleAxis::ValueName(uint32_t value) const {
        ASSERT(value < Count(), "RuleAxis::ValueName: value out of range");
        return _valueNames[value];
    }

    Optional<uint32_t> RuleAxis::FindValue(StringView valueName) const {
        const auto it = _valueIndex.find(valueName);
        if (it == _valueIndex.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    Span<const TagId> RuleAxis::ValueTags(uint32_t value) const {
        ASSERT(value < Count(), "RuleAxis::ValueTags: value out of range");
        return Span<const TagId>(_valueTags[value].data(), _valueTags[value].size());
    }

    bool RuleAxis::Matches(const RuleSelector& selector, uint32_t value) const {
        switch (selector.Kind()) {
            case RuleSelectorKind::Any:
                return true;
            case RuleSelectorKind::Id:
                return selector.Value() == value;
            case RuleSelectorKind::Tags: {
                ASSERT(value < Count(), "RuleAxis::Matches: value out of range");
                const TagRange range = selector.Range();
                const Vector<TagId>& tags = _valueTags[value];
                const auto first = eastl::lower_bound(tags.begin(), tags.end(), range.lo);
                return first != tags.end() && *first <= range.hi;
            }
        }
        return false;
    }

    Vector<uint32_t> RuleAxis::Expand(const RuleSelector& selector) const {
        Vector<uint32_t> values;
        for (uint32_t value = 0; value < Count(); ++value) {
            if (Matches(selector, value)) {
                values.push_back(value);
            }
        }
        return values;
    }

    std::expected<RuleSelector, String> RuleAxis::ParseSelector(StringView text) const {
        const StringView trimmed = Trim(text);
        const auto fail = [&](StringView reason) { return std::unexpected(Format("axis '{}': selector '{}' {}", _name, text, reason)); };
        if (trimmed.empty()) {
            return fail("is empty");
        }

        RuleSelector selector = RuleSelector::Any();
        if (trimmed == kAnyText) {
            return selector;
        }
        if (StartsWith(trimmed, kTagPrefix)) {
            StringView path = Trim(trimmed.substr(kTagPrefix.size()));
            const bool descendantsOnly = EndsWith(path, kDescendantsSuffix);
            if (descendantsOnly) {
                path = path.substr(0, path.size() - kDescendantsSuffix.size());
            }
            const Optional<TagId> tag = _tags.Find(path);
            if (!tag) {
                return fail(Format("names the unknown tag '{}'", path).c_str());
            }
            const uint32_t depthNumber = 2 * _tags.Depth(*tag);
            selector = descendantsOnly ? RuleSelector::Tags(_tags.DescendantRange(*tag), depthNumber + 1) : RuleSelector::Tags(_tags.Range(*tag), depthNumber);
        } else {
            const Optional<uint32_t> value = FindValue(trimmed);
            if (!value) {
                return fail("is not '*', a value name or 'tag:<path>'");
            }
            return RuleSelector::Id(*value);
        }

        if (Expand(selector).empty()) {
            return fail("matches no value");
        }
        return selector;
    }

}  // namespace BECore
