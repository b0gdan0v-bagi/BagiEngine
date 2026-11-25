#include "PanelCondition.h"

#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <cmath>

namespace BECore {

    namespace {

        constexpr StringView kAnd = "and";
        constexpr StringView kOr = "or";
        constexpr char kQuote = '"';
        constexpr char kEscape = '\\';

        struct Operator {
            StringView text;
            PanelCompare compare;
        };

        constexpr Operator kOperators[] = {
            {"==", PanelCompare::Equal}, {"!=", PanelCompare::NotEqual}, {"<=", PanelCompare::LessEqual}, {">=", PanelCompare::GreaterEqual}, {"<", PanelCompare::Less}, {">", PanelCompare::Greater},
        };

        bool IsBlank(char c) {
            return c == ' ' || c == '\t';
        }

        bool IsWordStart(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
        }

        bool IsWordChar(char c) {
            return IsWordStart(c) || (c >= '0' && c <= '9');
        }

        bool IsNumberStart(char c) {
            return (c >= '0' && c <= '9') || c == '-' || c == '.';
        }

        /// Splits at blanks outside a quoted key of a path; an unterminated quote runs to the end and fails in the path's parser
        Vector<StringView> Tokenize(StringView text) {
            Vector<StringView> tokens;
            size_t i = 0;
            while (i < text.size()) {
                if (IsBlank(text[i])) {
                    ++i;
                    continue;
                }
                const size_t start = i;
                bool quoted = false;
                while (i < text.size() && (quoted || !IsBlank(text[i]))) {
                    if (quoted && text[i] == kEscape) {
                        ++i;
                    } else if (text[i] == kQuote) {
                        quoted = !quoted;
                    }
                    ++i;
                }
                tokens.push_back(text.substr(start, std::min(i, text.size()) - start));
            }
            return tokens;
        }

        const Operator* FindOperator(StringView token) {
            for (const Operator& op : kOperators) {
                if (op.text == token) {
                    return &op;
                }
            }
            return nullptr;
        }

        bool IsOrder(PanelCompare compare) {
            return compare != PanelCompare::Equal && compare != PanelCompare::NotEqual;
        }

        std::expected<void, String> ReadLiteral(StringView token, PanelConditionTerm& term) {
            if (token.size() > PanelAssetLimits::kMaxTextBytes) {
                return std::unexpected(String("the literal is too long"));
            }
            if (!token.empty() && IsNumberStart(token.front())) {
                const Optional<double> number = StringUtils::ParseNumber<double>(token);
                if (!number || !std::isfinite(*number)) {
                    return std::unexpected(String("the literal is not a finite number"));
                }
                term.numeric = true;
                term.number = *number;
                return {};
            }
            if (token.empty() || !IsWordStart(token.front())) {
                return std::unexpected(String("the literal is a number or a word of letters, digits and '_'"));
            }
            for (const char c : token) {
                if (!IsWordChar(c)) {
                    return std::unexpected(String("the literal is a number or a word of letters, digits and '_'"));
                }
            }
            if (IsOrder(term.compare)) {
                return std::unexpected(String("an order compare takes a number"));
            }
            term.word = String(token);
            return {};
        }

        bool Compare(PanelCompare compare, double value, double literal) {
            switch (compare) {
                case PanelCompare::Equal:
                    return value == literal;
                case PanelCompare::NotEqual:
                    return value != literal;
                case PanelCompare::Less:
                    return value < literal;
                case PanelCompare::LessEqual:
                    return value <= literal;
                case PanelCompare::Greater:
                    return value > literal;
                case PanelCompare::GreaterEqual:
                    return value >= literal;
                case PanelCompare::Truth:
                    break;
            }
            return false;
        }

        bool EvaluateTerm(PanelBindingReader& reader, const PanelDataSources& sources, const PanelConditionTerm& term, bool whenUnreadable, String& scratch) {
            if (term.compare == PanelCompare::Truth) {
                bool value = false;
                return reader.ReadBool(sources, term.binding, value) == PanelReadStatus::Ok ? value : whenUnreadable;
            }
            if (term.numeric) {
                double value = 0.0;
                return reader.ReadNumber(sources, term.binding, value) == PanelReadStatus::Ok ? Compare(term.compare, value, term.number) : whenUnreadable;
            }
            StringView text;
            if (IsOrder(term.compare) || reader.ReadText(sources, term.binding, scratch, text) != PanelReadStatus::Ok) {
                return whenUnreadable;
            }
            return (text == StringView(term.word)) == (term.compare == PanelCompare::Equal);
        }

    }  // namespace

    std::expected<PanelCondition, String> ParsePanelCondition(StringView text) {
        if (text.size() > PanelAssetLimits::kMaxTextBytes) {
            return std::unexpected(String("the condition is too long"));
        }
        const Vector<StringView> tokens = Tokenize(text);
        if (tokens.empty()) {
            return std::unexpected(String("the condition is empty"));
        }

        PanelCondition condition;
        condition.alternatives.emplace_back();
        size_t termCount = 0;
        size_t i = 0;
        while (true) {
            if (++termCount > PanelAssetLimits::kMaxConditionTerms) {
                return std::unexpected(Format("more than {} terms", PanelAssetLimits::kMaxConditionTerms));
            }
            PanelConditionTerm term;
            auto binding = ParseCondition(tokens[i]);
            if (!binding) {
                return std::unexpected(std::move(binding.error()));
            }
            term.binding = std::move(*binding);
            ++i;

            if (i < tokens.size()) {
                if (const Operator* op = FindOperator(tokens[i])) {
                    if (term.binding.negated) {
                        return std::unexpected(String("'!' cannot be combined with a compare"));
                    }
                    if (i + 1 >= tokens.size()) {
                        return std::unexpected(String("a literal is missing after the compare"));
                    }
                    term.compare = op->compare;
                    if (auto literal = ReadLiteral(tokens[i + 1], term); !literal) {
                        return std::unexpected(std::move(literal.error()));
                    }
                    i += 2;
                }
            }
            condition.alternatives.back().push_back(std::move(term));

            if (i >= tokens.size()) {
                return condition;
            }
            if (tokens[i] == kOr) {
                condition.alternatives.emplace_back();
            } else if (tokens[i] != kAnd) {
                return std::unexpected(String("expected 'and', 'or' or a compare after a term, tokens are separated by blanks"));
            }
            ++i;
            if (i >= tokens.size()) {
                return std::unexpected(Format("a term is missing after '{}'", tokens[i - 1]));
            }
        }
    }

    bool EvaluatePanelCondition(PanelBindingReader& reader, const PanelDataSources& sources, const PanelCondition& condition, bool whenUnreadable, String& scratch) {
        if (condition.alternatives.empty()) {
            return true;
        }
        for (const Vector<PanelConditionTerm>& all : condition.alternatives) {
            bool holds = true;
            for (const PanelConditionTerm& term : all) {
                if (!EvaluateTerm(reader, sources, term, whenUnreadable, scratch)) {
                    holds = false;
                    break;
                }
            }
            if (holds) {
                return true;
            }
        }
        return false;
    }

}  // namespace BECore
