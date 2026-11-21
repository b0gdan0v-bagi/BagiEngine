#include "ValueTooltip.h"

namespace BECore {

    namespace {

        constexpr StringView kValueOnlySuffix = "value";
        constexpr StringView kNoValueText = "?";
        constexpr StringView kExtractLabel = "Extract";
        /// A formula longer than this (a fold over a long list) shows its value only: a tooltip is read by a person
        constexpr size_t kMaxPartsBytes = 256;
        /// The nodes one rendering may visit: a hand-built program can share nodes between parents and the expanded tree would be exponential
        constexpr uint32_t kMaxVisitedNodes = kMaxTraceNodes * 4;

        /// How tightly an operator binds: a child that binds looser than its parent gets parentheses
        constexpr int kPrecedenceOr = 1;
        constexpr int kPrecedenceAnd = 2;
        constexpr int kPrecedenceEquality = 3;
        constexpr int kPrecedenceOrder = 4;
        constexpr int kPrecedenceSum = 5;
        constexpr int kPrecedenceProduct = 6;
        constexpr int kPrecedenceUnary = 7;
        constexpr int kPrecedenceAtom = 8;

        struct Rendered {
            String text;
            int precedence = kPrecedenceAtom;
        };

        struct Infix {
            StringView symbol;
            int precedence = 0;
        };

        [[nodiscard]] Optional<Infix> InfixOf(ProgramOperation operation) {
            switch (operation) {
                case ProgramOperation::Add:
                    return Infix{"+", kPrecedenceSum};
                case ProgramOperation::Sub:
                    return Infix{"-", kPrecedenceSum};
                case ProgramOperation::Mul:
                    return Infix{"*", kPrecedenceProduct};
                case ProgramOperation::Div:
                    return Infix{"/", kPrecedenceProduct};
                case ProgramOperation::Mod:
                    return Infix{"%", kPrecedenceProduct};
                case ProgramOperation::Eq:
                    return Infix{"==", kPrecedenceEquality};
                case ProgramOperation::Ne:
                    return Infix{"!=", kPrecedenceEquality};
                case ProgramOperation::Lt:
                    return Infix{"<", kPrecedenceOrder};
                case ProgramOperation::Le:
                    return Infix{"<=", kPrecedenceOrder};
                case ProgramOperation::Gt:
                    return Infix{">", kPrecedenceOrder};
                case ProgramOperation::Ge:
                    return Infix{">=", kPrecedenceOrder};
                case ProgramOperation::And:
                    return Infix{"&&", kPrecedenceAnd};
                case ProgramOperation::Or:
                    return Infix{"||", kPrecedenceOr};
                default:
                    return std::nullopt;
            }
        }

        [[nodiscard]] bool IsNumber(ProgramType type) {
            return type == ProgramType::Int32 || type == ProgramType::Int64 || type == ProgramType::Float || type == ProgramType::Double;
        }

        [[nodiscard]] String Lowercase(StringView text) {
            String lower;
            for (const char ch : text) {
                lower += (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
            }
            return lower;
        }

        /// The name of the conversion function of the language that makes @p type
        [[nodiscard]] StringView ConversionName(ProgramType type) {
            switch (type) {
                case ProgramType::Int64:
                    return "int64";
                case ProgramType::Float:
                    return "float";
                case ProgramType::Double:
                    return "double";
                default:
                    return "int";
            }
        }

        /// @p pattern with `{0}` and `{1}` replaced by hand: the pattern is a translator's text and never a format string
        [[nodiscard]] String FillPattern(StringView pattern, StringView number, StringView label) {
            String text;
            for (size_t i = 0; i < pattern.size(); ++i) {
                if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && (pattern[i + 1] == '0' || pattern[i + 1] == '1')) {
                    const StringView word = pattern[i + 1] == '0' ? number : label;
                    text.append(word.data(), word.size());
                    i += 2;
                } else {
                    text += pattern[i];
                }
            }
            return text;
        }

        /// Writes the formula of a trace as a person would say it
        class PartsWriter {
        public:
            PartsWriter(const ProgramTrace& trace, const TooltipWords& words) : _trace(trace), _words(words), _constant(trace.nodes.size(), kUnknown) {}

            /// The formula of the root, empty when it is too long or the trace has no root
            [[nodiscard]] String Write() {
                if (_trace.root == kNoTraceNode) {
                    return String();
                }
                Rendered root = Render(_trace.root);
                return (_overflow || root.text.size() > kMaxPartsBytes) ? String() : std::move(root.text);
            }

        private:
            static constexpr int8_t kUnknown = 0;
            static constexpr int8_t kIsConstant = 1;
            static constexpr int8_t kNotConstant = 2;

            /// A node that holds a constant or is computed from constants alone: it folds to its value
            bool IsConstant(uint32_t index) {
                if (_constant[index] != kUnknown) {
                    return _constant[index] == kIsConstant;
                }
                const TraceNode& node = _trace.nodes[index];
                bool constant = node.kind == TraceStepKind::Constant;
                if (!constant && (node.kind == TraceStepKind::Operation || node.kind == TraceStepKind::Conversion || node.kind == TraceStepKind::Table)) {
                    constant = !_trace.Children(node).empty();
                    for (const uint32_t child : _trace.Children(node)) {
                        constant = constant && IsConstant(child);
                    }
                }
                _constant[index] = constant ? kIsConstant : kNotConstant;
                return constant;
            }

            [[nodiscard]] bool IsKey(uint32_t index) const {
                return _trace.nodes[index].kind == TraceStepKind::ContextKey && IsNumber(_trace.nodes[index].type);
            }

            [[nodiscard]] bool IsNumberConstant(uint32_t index) {
                return IsNumber(_trace.nodes[index].type) && IsConstant(index);
            }

            [[nodiscard]] String LabelOf(const String& path) const {
                if (_words.label) {
                    if (Optional<String> label = _words.label(path)) {
                        return std::move(*label);
                    }
                }
                return path;
            }

            [[nodiscard]] static String Wrap(Rendered rendered, int least) {
                return rendered.precedence < least ? Format("({})", rendered.text) : std::move(rendered.text);
            }

            [[nodiscard]] static Rendered Join(Rendered left, StringView symbol, int precedence, Rendered right) {
                // Left to right: an equal precedence on the right is a different grouping
                return Rendered{Format("{} {} {}", Wrap(std::move(left), precedence), symbol, Wrap(std::move(right), precedence + 1)), precedence};
            }

            [[nodiscard]] Rendered Function(StringView name, Span<const uint32_t> arguments) {
                String text = String(name) + "(";
                for (size_t i = 0; i < arguments.size(); ++i) {
                    text += Format("{}{}", i == 0 ? "" : ", ", Render(arguments[i]).text);
                }
                text += ")";
                return Rendered{std::move(text), kPrecedenceAtom};
            }

            [[nodiscard]] Rendered PerText(uint32_t number, uint32_t key) {
                return Rendered{FillPattern(_words.perPattern, FormatNodeValue(_trace.nodes[number]), LabelOf(_trace.nodes[key].label)), kPrecedenceAtom};
            }

            /// `number * field` in either order is the words' `per` pattern; any other product is infix
            [[nodiscard]] Rendered Product(uint32_t left, uint32_t right) {
                if (IsNumberConstant(left) && IsKey(right)) {
                    return PerText(left, right);
                }
                if (IsNumberConstant(right) && IsKey(left)) {
                    return PerText(right, left);
                }
                Rendered first = Render(left);
                return Join(std::move(first), "*", kPrecedenceProduct, Render(right));
            }

            [[nodiscard]] Rendered Operation(const TraceNode& node, Span<const uint32_t> children) {
                if (!node.operation.has_value()) {
                    if (node.label == kMulAddLabel && children.size() == 3) {
                        return Join(Product(children[0], children[1]), "+", kPrecedenceSum, Render(children[2]));
                    }
                    return Function(node.label, children);
                }
                const ProgramOperation operation = *node.operation;
                if (children.size() == 2) {
                    if (operation == ProgramOperation::Mul) {
                        return Product(children[0], children[1]);
                    }
                    if (const Optional<Infix> infix = InfixOf(operation)) {
                        Rendered first = Render(children[0]);
                        return Join(std::move(first), infix->symbol, infix->precedence, Render(children[1]));
                    }
                }
                if (children.size() == 1 && (operation == ProgramOperation::Neg || operation == ProgramOperation::Not)) {
                    return Rendered{Format("{}{}", operation == ProgramOperation::Neg ? "-" : "!", Wrap(Render(children[0]), kPrecedenceUnary)), kPrecedenceUnary};
                }
                if (operation == ProgramOperation::Extract && children.size() == 1 && node.label.size() > kExtractLabel.size()) {
                    // The label is `Extract .y`: the component follows the operand like a member
                    return Rendered{Format("{}{}", Wrap(Render(children[0]), kPrecedenceAtom), StringView(node.label.c_str() + kExtractLabel.size() + 1)), kPrecedenceAtom};
                }
                if (operation == ProgramOperation::Pack) {
                    return Function(Lowercase(EnumUtils<ProgramType>::ToString(node.type)), children);
                }
                return Function(Lowercase(EnumUtils<ProgramOperation>::ToString(operation)), children);
            }

            [[nodiscard]] Rendered Render(uint32_t index) {
                if (_visited >= kMaxVisitedNodes) {
                    _overflow = true;
                    return Rendered{};
                }
                ++_visited;
                const TraceNode& node = _trace.nodes[index];
                if (IsConstant(index)) {
                    return Rendered{FormatNodeValue(node), kPrecedenceAtom};
                }
                const Span<const uint32_t> children = _trace.Children(node);
                switch (node.kind) {
                    case TraceStepKind::Constant:
                    case TraceStepKind::ContextKey:
                    case TraceStepKind::List:
                        return Rendered{LabelOf(node.label), kPrecedenceAtom};
                    case TraceStepKind::Table:
                        return Rendered{children.empty() ? String("table") : Format("table[{}]", Render(children[0]).text), kPrecedenceAtom};
                    case TraceStepKind::Conversion:
                        return Function(ConversionName(node.type), children);
                    case TraceStepKind::Call:
                        return Function(node.label, children);
                    case TraceStepKind::Operation:
                        return Operation(node, children);
                }
                return Rendered{};
            }

            const ProgramTrace& _trace;
            const TooltipWords& _words;
            Vector<int8_t> _constant;
            uint32_t _visited = 0;
            bool _overflow = false;
        };

        /// The value of a binding with the parts of its formula after it when they say more than the value does
        [[nodiscard]] String ValueWithParts(const ValueExplanation& explanation, const TooltipWords& words) {
            if (!explanation.hasValue) {
                return String(kNoValueText);
            }
            if (explanation.kind != ValueKind::Program) {
                return explanation.valueText;
            }
            const String parts = PartsWriter(explanation.trace, words).Write();
            if (parts.empty() || parts == explanation.valueText) {
                return explanation.valueText;
            }
            return Format("{} ({})", explanation.valueText, parts);
        }

        [[nodiscard]] const TooltipBinding* FindBinding(Span<const TooltipBinding> bindings, StringView name) {
            for (const TooltipBinding& binding : bindings) {
                if (binding.name == name && binding.explanation != nullptr) {
                    return &binding;
                }
            }
            return nullptr;
        }

        /// What a `{name}` or `{name:value}` between the braces stands for; empty when no binding has the name
        [[nodiscard]] Optional<String> Expand(StringView inner, Span<const TooltipBinding> bindings, const TooltipWords& words) {
            bool valueOnly = false;
            StringView name = inner;
            const size_t colon = inner.rfind(':');
            if (colon != StringView::npos && inner.substr(colon + 1) == kValueOnlySuffix) {
                valueOnly = true;
                name = inner.substr(0, colon);
            }
            const TooltipBinding* binding = FindBinding(bindings, name);
            if (binding == nullptr) {
                return std::nullopt;
            }
            if (valueOnly) {
                return binding->explanation->hasValue ? binding->explanation->valueText : String(kNoValueText);
            }
            return ValueWithParts(*binding->explanation, words);
        }

    }  // namespace

    String FormatTooltip(StringView templateText, Span<const TooltipBinding> bindings, const TooltipWords& words) {
        String text;
        const size_t size = templateText.size();
        for (size_t i = 0; i < size; ++i) {
            const char ch = templateText[i];
            if (ch == '{' && i + 1 < size && templateText[i + 1] == '{') {
                text += '{';
                ++i;
            } else if (ch == '}' && i + 1 < size && templateText[i + 1] == '}') {
                text += '}';
                ++i;
            } else if (ch == '{') {
                const size_t close = templateText.find_first_of("{}", i + 1);
                if (close != StringView::npos && templateText[close] == '}') {
                    if (Optional<String> expanded = Expand(templateText.substr(i + 1, close - i - 1), bindings, words)) {
                        text += *expanded;
                        i = close;
                        continue;
                    }
                }
                // An unknown name, an unclosed brace: the template's mistake stays visible
                text += ch;
            } else {
                text += ch;
            }
        }
        return text;
    }

}  // namespace BECore
