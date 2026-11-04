#include "UICssParser.h"

#include <BECore/UI/UIValueText.h>
#include <BECore/UI/UIWebText.h>

namespace BECore {

    namespace {

        constexpr uint32_t IdSpecificity = 1u << 16;
        constexpr uint32_t ClassSpecificity = 1u << 8;
        constexpr uint32_t TagSpecificity = 1u;
        constexpr eastl::string_view ImportantSuffix = "important";

        bool IsIdentChar(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        }

        /// Line numbers of positions in one text
        class LineIndex {
        public:
            LineIndex(eastl::string_view text, uint32_t firstLine) : _begin(text.data()), _firstLine(firstLine) {
                _newlines.push_back(0);
                for (size_t i = 0; i < text.size(); ++i) {
                    if (text[i] == '\n') {
                        _newlines.push_back(i + 1);
                    }
                }
            }

            /// 1-based line of the character at @p pointer
            [[nodiscard]] uint32_t LineOf(const char* pointer) const {
                const size_t offset = static_cast<size_t>(pointer - _begin);
                const auto it = std::upper_bound(_newlines.begin(), _newlines.end(), offset);
                return static_cast<uint32_t>(it - _newlines.begin()) + _firstLine - 1;
            }

        private:
            const char* _begin = nullptr;
            uint32_t _firstLine = 1;
            eastl::vector<size_t> _newlines;
        };

        /// The text with the comments replaced by spaces (line breaks stay, so line numbers do)
        eastl::string StripComments(eastl::string_view css) {
            eastl::string out(css.data(), css.size());
            size_t i = 0;
            while (i + 1 < out.size()) {
                if (out[i] == '/' && out[i + 1] == '*') {
                    size_t end = out.find("*/", i + 2);
                    end = end == eastl::string::npos ? out.size() : end + 2;
                    for (size_t j = i; j < end; ++j) {
                        if (out[j] != '\n') {
                            out[j] = ' ';
                        }
                    }
                    i = end;
                } else {
                    ++i;
                }
            }
            return out;
        }

        /// The index of the '}' that closes the '{' at @p open, or the end of the text when there is none
        size_t FindClosingBrace(eastl::string_view text, size_t open) {
            int depth = 0;
            char quote = 0;
            for (size_t i = open; i < text.size(); ++i) {
                const char c = text[i];
                if (quote != 0) {
                    if (c == quote) {
                        quote = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '{') {
                    ++depth;
                } else if (c == '}') {
                    if (--depth == 0) {
                        return i;
                    }
                }
            }
            return text.size();
        }

        std::optional<UICssPseudo> ParsePseudo(eastl::string_view name) {
            if (name == "hover") {
                return UICssPseudo::Hover;
            }
            if (name == "active") {
                return UICssPseudo::Active;
            }
            if (name == "disabled") {
                return UICssPseudo::Disabled;
            }
            if (name == "checked") {
                return UICssPseudo::Checked;
            }
            return std::nullopt;
        }

        /// Reads one selector. On failure @p why says what is outside the subset
        std::optional<UICssSelector> ParseSelector(eastl::string_view text, eastl::string& why) {
            UICssSelector selector;
            size_t pos = 0;
            UICssCombinator pending = UICssCombinator::None;
            while (pos < text.size()) {
                const bool spaced = UIWebText::IsSpace(text[pos]);
                while (pos < text.size() && UIWebText::IsSpace(text[pos])) {
                    ++pos;
                }
                if (pos >= text.size()) {
                    break;
                }
                if (spaced && !selector.compounds.empty() && pending == UICssCombinator::None) {
                    pending = UICssCombinator::Descendant;
                }
                const char c = text[pos];
                if (c == '>') {
                    pending = UICssCombinator::Child;
                    ++pos;
                    continue;
                }
                if (c == '+' || c == '~') {
                    why = Format("the combinator '{}'", c);
                    return std::nullopt;
                }

                UICssCompound compound;
                compound.combinator = selector.compounds.empty() ? UICssCombinator::None : pending;
                pending = UICssCombinator::None;
                if (c == '*') {
                    ++pos;
                } else if (IsIdentChar(c)) {
                    const size_t start = pos;
                    while (pos < text.size() && IsIdentChar(text[pos])) {
                        ++pos;
                    }
                    compound.tag = UIWebText::ToLower(text.substr(start, pos - start));
                    selector.specificity += TagSpecificity;
                }
                bool any = !compound.tag.empty() || c == '*';
                while (pos < text.size() && !UIWebText::IsSpace(text[pos]) && text[pos] != '>') {
                    const char marker = text[pos];
                    if (marker == '[') {
                        why = "an attribute selector";
                        return std::nullopt;
                    }
                    if (marker != '#' && marker != '.' && marker != ':') {
                        why = Format("the character '{}'", marker);
                        return std::nullopt;
                    }
                    ++pos;
                    if (marker == ':' && pos < text.size() && text[pos] == ':') {
                        why = "a pseudo-element";
                        return std::nullopt;
                    }
                    const size_t start = pos;
                    while (pos < text.size() && IsIdentChar(text[pos])) {
                        ++pos;
                    }
                    const eastl::string_view name = text.substr(start, pos - start);
                    if (name.empty()) {
                        why = "an empty name";
                        return std::nullopt;
                    }
                    if (marker == '#') {
                        compound.id = eastl::string(name);
                        selector.specificity += IdSpecificity;
                    } else if (marker == '.') {
                        compound.classes.push_back(eastl::string(name));
                        selector.specificity += ClassSpecificity;
                    } else {
                        const auto pseudo = ParsePseudo(UIWebText::ToLower(name));
                        if (!pseudo) {
                            why = Format("the pseudo-class ':{}'", eastl::string(name).c_str());
                            return std::nullopt;
                        }
                        compound.pseudo = *pseudo;
                        selector.specificity += ClassSpecificity;
                    }
                    any = true;
                }
                if (!any) {
                    why = "an empty selector";
                    return std::nullopt;
                }
                selector.compounds.push_back(std::move(compound));
            }
            if (selector.compounds.empty()) {
                why = "an empty selector";
                return std::nullopt;
            }
            for (size_t i = 0; i + 1 < selector.compounds.size(); ++i) {
                if (selector.compounds[i].pseudo != UICssPseudo::None) {
                    why = "a pseudo-class that is not on the last part of the selector";
                    return std::nullopt;
                }
            }
            return selector;
        }

        /// "16/9", "16 / 9" or "1.5"
        std::optional<float> ParseRatio(eastl::string_view text) {
            const size_t slash = text.find('/');
            if (slash == eastl::string_view::npos) {
                return UIText::ParseFloat(text);
            }
            const auto numerator = UIText::ParseFloat(text.substr(0, slash));
            const auto denominator = UIText::ParseFloat(text.substr(slash + 1));
            if (!numerator || !denominator || *denominator <= 0.0f) {
                return std::nullopt;
            }
            return *numerator / *denominator;
        }

        /// One media query: `screen and (orientation: portrait) and (min-width: 600px)`. On failure @p why says what is outside the subset
        std::optional<UIVariantCondition> ParseMediaQuery(eastl::string_view query, eastl::string& why) {
            UIVariantCondition condition;
            for (const eastl::string_view word : UIWebText::SplitWords(query)) {
                const eastl::string lower = UIWebText::ToLower(word);
                if (lower == "and" || lower == "screen" || lower == "all") {
                    continue;
                }
                if (lower.front() != '(' || lower.back() != ')') {
                    why = Format("the media query part '{}'", eastl::string(word).c_str());
                    return std::nullopt;
                }
                const eastl::string_view feature = eastl::string_view(lower).substr(1, lower.size() - 2);
                const size_t colon = feature.find(':');
                if (colon == eastl::string_view::npos) {
                    why = Format("the media feature '{}' without a value", eastl::string(feature).c_str());
                    return std::nullopt;
                }
                const eastl::string name(UIWebText::TrimSpace(feature.substr(0, colon)));
                const eastl::string_view value = UIWebText::TrimSpace(feature.substr(colon + 1));
                if (name == "orientation") {
                    if (value == "portrait") {
                        condition.orientation = UIOrientation::Portrait;
                    } else if (value == "landscape") {
                        condition.orientation = UIOrientation::Landscape;
                    } else {
                        why = Format("the orientation '{}'", eastl::string(value).c_str());
                        return std::nullopt;
                    }
                    continue;
                }
                const bool isAspect = name == "aspect-ratio" || name == "min-aspect-ratio" || name == "max-aspect-ratio";
                const std::optional<float> number = isAspect ? ParseRatio(value) : UIWebText::ParsePixels(value);
                if (!number) {
                    why = Format("the value '{}' of '{}'", eastl::string(value).c_str(), name.c_str());
                    return std::nullopt;
                }
                if (name == "min-width") {
                    condition.minWidth = *number;
                } else if (name == "max-width") {
                    condition.maxWidth = *number;
                } else if (name == "width") {
                    condition.minWidth = condition.maxWidth = *number;
                } else if (name == "min-height") {
                    condition.minHeight = *number;
                } else if (name == "max-height") {
                    condition.maxHeight = *number;
                } else if (name == "height") {
                    condition.minHeight = condition.maxHeight = *number;
                } else if (name == "min-aspect-ratio") {
                    condition.minAspect = *number;
                } else if (name == "max-aspect-ratio") {
                    condition.maxAspect = *number;
                } else if (name == "aspect-ratio") {
                    condition.minAspect = condition.maxAspect = *number;
                } else {
                    why = Format("the media feature '{}'", name.c_str());
                    return std::nullopt;
                }
            }
            return condition;
        }

        eastl::vector<UICssDeclaration> ReadDeclarations(eastl::string_view body, const LineIndex& lines, uint32_t sourceId) {
            eastl::vector<UICssDeclaration> declarations;
            for (const eastl::string_view part : UIWebText::SplitTopLevel(body, ';')) {
                if (part.empty()) {
                    continue;
                }
                const size_t colon = part.find(':');
                if (colon == eastl::string_view::npos) {
                    continue;
                }
                UICssDeclaration declaration;
                declaration.property = UIWebText::ToLower(UIWebText::TrimSpace(part.substr(0, colon)));
                eastl::string_view value = UIWebText::TrimSpace(part.substr(colon + 1));
                // "... !important", also written "! important"
                const size_t bang = value.rfind('!');
                if (bang != eastl::string_view::npos && UIWebText::ToLower(UIWebText::TrimSpace(value.substr(bang + 1))) == ImportantSuffix) {
                    declaration.important = true;
                    value = UIWebText::TrimSpace(value.substr(0, bang));
                }
                declaration.value = eastl::string(value);
                declaration.line = lines.LineOf(part.data());
                declaration.sourceId = sourceId;
                if (!declaration.property.empty() && !declaration.value.empty()) {
                    declarations.push_back(std::move(declaration));
                }
            }
            return declarations;
        }

        class SheetReader {
        public:
            SheetReader(UICssSheet& sheet, eastl::string_view text, uint32_t firstLine, uint32_t sourceId, UIWebReport& report)
                : _sheet(sheet), _text(text), _lines(text, firstLine), _sourceId(sourceId), _report(report) {}

            void ReadRules(size_t begin, size_t end, int media) {
                size_t pos = begin;
                while (pos < end) {
                    while (pos < end && UIWebText::IsSpace(_text[pos])) {
                        ++pos;
                    }
                    if (pos >= end) {
                        return;
                    }
                    if (_text[pos] == '@') {
                        pos = ReadAtRule(pos, end, media);
                        continue;
                    }
                    const size_t open = _text.find('{', pos);
                    if (open == eastl::string_view::npos || open >= end) {
                        Report(pos, "a rule without a body is left out");
                        return;
                    }
                    const size_t close = std::min(FindClosingBrace(_text, open), end);
                    ReadRule(_text.substr(pos, open - pos), _text.substr(open + 1, close - open - 1), media);
                    pos = close + 1;
                }
            }

        private:
            void Report(size_t pos, eastl::string message) {
                _report.AddOnce(_sheet.sources[_sourceId], _lines.LineOf(_text.data() + std::min(pos, _text.size() - 1)), std::move(message));
            }

            size_t ReadAtRule(size_t pos, size_t end, int media) {
                size_t nameEnd = pos + 1;
                while (nameEnd < end && IsIdentChar(_text[nameEnd])) {
                    ++nameEnd;
                }
                const eastl::string name = UIWebText::ToLower(_text.substr(pos + 1, nameEnd - pos - 1));
                const size_t semicolon = _text.find(';', nameEnd);
                const size_t open = _text.find('{', nameEnd);
                const bool hasBlock = open != eastl::string_view::npos && open < end && (semicolon == eastl::string_view::npos || open < semicolon);
                if (!hasBlock) {
                    Report(pos, Format("@{} is not supported", name.c_str()));
                    return semicolon == eastl::string_view::npos || semicolon >= end ? end : semicolon + 1;
                }
                const size_t close = std::min(FindClosingBrace(_text, open), end);
                if (name != "media") {
                    Report(pos, Format("@{} is not supported", name.c_str()));
                } else if (media != -1) {
                    Report(pos, "an @media inside an @media is not supported");
                } else {
                    ReadMedia(pos, _text.substr(nameEnd, open - nameEnd), open, close);
                }
                return close + 1;
            }

            void ReadMedia(size_t pos, eastl::string_view prelude, size_t open, size_t close) {
                UICssMedia block;
                block.line = _lines.LineOf(_text.data() + pos);
                block.sourceId = _sourceId;
                for (const eastl::string_view query : UIWebText::SplitTopLevel(prelude, ',')) {
                    eastl::string why;
                    if (auto condition = ParseMediaQuery(query, why)) {
                        block.alternatives.push_back(*condition);
                    } else {
                        Report(pos, Format("@media query '{}' is left out: {} is not supported", eastl::string(query).c_str(), why.c_str()));
                    }
                }
                if (block.alternatives.empty()) {
                    return;
                }
                ++_report.mediaBlocks;
                const int index = static_cast<int>(_sheet.medias.size());
                _sheet.medias.push_back(std::move(block));
                ReadRules(open + 1, close, index);
            }

            void ReadRule(eastl::string_view prelude, eastl::string_view body, int media) {
                UICssRule rule;
                rule.media = media;
                for (const eastl::string_view text : UIWebText::SplitTopLevel(prelude, ',')) {
                    eastl::string why;
                    if (auto selector = ParseSelector(text, why)) {
                        rule.selectors.push_back(std::move(*selector));
                    } else {
                        Report(static_cast<size_t>(text.data() - _text.data()), Format("selector '{}' is left out: {} is not supported", eastl::string(text).c_str(), why.c_str()));
                    }
                }
                if (rule.selectors.empty()) {
                    return;
                }
                rule.declarations = ReadDeclarations(body, _lines, _sourceId);
                rule.order = static_cast<uint32_t>(_sheet.rules.size());
                _report.declarations += static_cast<uint32_t>(rule.declarations.size());
                ++_report.rules;
                _sheet.rules.push_back(std::move(rule));
            }

            UICssSheet& _sheet;
            eastl::string_view _text;
            LineIndex _lines;
            uint32_t _sourceId;
            UIWebReport& _report;
        };

    }  // namespace

    void UICssParser::ParseInto(UICssSheet& sheet, eastl::string_view css, eastl::string_view sourceName, uint32_t firstLine, UIWebReport& report) {
        const eastl::string text = StripComments(css);
        const uint32_t sourceId = static_cast<uint32_t>(sheet.sources.size());
        sheet.sources.push_back(eastl::string(sourceName));
        SheetReader(sheet, text, firstLine, sourceId, report).ReadRules(0, text.size(), -1);
    }

    eastl::vector<UICssDeclaration> UICssParser::ParseDeclarations(eastl::string_view text, uint32_t line, uint32_t sourceId, UIWebReport& /*report*/) {
        const eastl::string stripped = StripComments(text);
        const LineIndex lines(stripped, line);
        return ReadDeclarations(stripped, lines, sourceId);
    }

}  // namespace BECore
