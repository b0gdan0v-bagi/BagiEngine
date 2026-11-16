#include "ExprParser.h"

#include <BECore/Values/Programs/ExprLexer.h>

namespace BECore {

    namespace {

        /// Binary operators by the token that writes them, with the level they bind at (higher binds tighter); level 0 is no operator
        struct BinaryOperator {
            ProgramOperation operation = ProgramOperation::Add;
            int precedence = 0;
        };

        constexpr int kPrecedenceOr = 1;
        constexpr int kPrecedenceAnd = 2;
        constexpr int kPrecedenceEquality = 3;
        constexpr int kPrecedenceRelational = 4;
        constexpr int kPrecedenceAdditive = 5;
        constexpr int kPrecedenceMultiplicative = 6;

        BinaryOperator BinaryOf(ExprTokenKind kind) {
            switch (kind) {
                case ExprTokenKind::OrOr:
                    return {ProgramOperation::Or, kPrecedenceOr};
                case ExprTokenKind::AndAnd:
                    return {ProgramOperation::And, kPrecedenceAnd};
                case ExprTokenKind::EqualEqual:
                    return {ProgramOperation::Eq, kPrecedenceEquality};
                case ExprTokenKind::BangEqual:
                    return {ProgramOperation::Ne, kPrecedenceEquality};
                case ExprTokenKind::Less:
                    return {ProgramOperation::Lt, kPrecedenceRelational};
                case ExprTokenKind::LessEqual:
                    return {ProgramOperation::Le, kPrecedenceRelational};
                case ExprTokenKind::Greater:
                    return {ProgramOperation::Gt, kPrecedenceRelational};
                case ExprTokenKind::GreaterEqual:
                    return {ProgramOperation::Ge, kPrecedenceRelational};
                case ExprTokenKind::Plus:
                    return {ProgramOperation::Add, kPrecedenceAdditive};
                case ExprTokenKind::Minus:
                    return {ProgramOperation::Sub, kPrecedenceAdditive};
                case ExprTokenKind::Star:
                    return {ProgramOperation::Mul, kPrecedenceMultiplicative};
                case ExprTokenKind::Slash:
                    return {ProgramOperation::Div, kPrecedenceMultiplicative};
                case ExprTokenKind::Percent:
                    return {ProgramOperation::Mod, kPrecedenceMultiplicative};
                default:
                    return {};
            }
        }

        using NodeResult = std::expected<ExprNodeId, ProgramError>;

        class Parser {
        public:
            explicit Parser(Vector<ExprToken> tokens) : _tokens(std::move(tokens)) {}

            std::expected<ExprAst, ProgramError> Run() {
                if (_tokens.front().kind == ExprTokenKind::End) {
                    return std::unexpected(ProgramError{"the formula is empty", 1});
                }
                const NodeResult root = ParseBinary(kPrecedenceOr);
                if (!root.has_value()) {
                    return std::unexpected(root.error());
                }
                const ExprToken& rest = Peek();
                if (rest.kind != ExprTokenKind::End) {
                    if (rest.kind == ExprTokenKind::RightParen) {
                        return std::unexpected(ProgramError{"unexpected ')': there is no '(' to close", rest.column});
                    }
                    return std::unexpected(ProgramError{Format("expected an operator or the end of the formula, found {}", DescribeToken(rest)), rest.column});
                }
                _ast.root = *root;
                return std::move(_ast);
            }

        private:
            [[nodiscard]] const ExprToken& Peek(size_t ahead = 0) const {
                return _tokens[std::min(_index + ahead, _tokens.size() - 1)];
            }

            const ExprToken& Take() {
                const ExprToken& token = _tokens[_index];
                if (_index + 1 < _tokens.size()) {
                    ++_index;
                }
                return token;
            }

            static std::unexpected<ProgramError> Fail(const ExprToken& at, String message) {
                return std::unexpected(ProgramError{std::move(message), at.column});
            }

            /// The levels of nodes below @p node: a leaf has none. The checker, the lowering and the printer recurse by this height, which the parentheses do not show, so a long
            /// left-nested chain (`a + b + c + ...`) is bounded here and not only by the node count
            [[nodiscard]] uint32_t HeightOf(const ExprNode& node) const {
                switch (node.kind) {
                    case ExprNodeKind::Unary:
                    case ExprNodeKind::Member:
                        return _heights[node.first] + 1;
                    case ExprNodeKind::Binary:
                        return std::max(_heights[node.first], _heights[node.second]) + 1;
                    case ExprNodeKind::Call: {
                        uint32_t below = 0;
                        for (uint32_t i = 0; i < node.argumentCount; ++i) {
                            below = std::max(below, _heights[_ast.arguments[node.firstArgument + i]] + 1);
                        }
                        return below;
                    }
                    default:
                        return 0;
                }
            }

            NodeResult Add(ExprNode node) {
                if (_ast.nodes.size() >= ExprLimits::kMaxNodes) {
                    return std::unexpected(ProgramError{Format("the formula has more than {} parts", ExprLimits::kMaxNodes), node.column});
                }
                const uint32_t height = HeightOf(node);
                if (height > ExprLimits::kMaxTreeHeight) {
                    return std::unexpected(ProgramError{Format("the formula is more than {} levels high (a long chain of operators counts)", ExprLimits::kMaxTreeHeight), node.column});
                }
                _ast.nodes.push_back(std::move(node));
                _heights.push_back(height);
                return static_cast<ExprNodeId>(_ast.nodes.size() - 1);
            }

            NodeResult ParseBinary(int minimum) {
                NodeResult left = ParseUnary();
                if (!left.has_value()) {
                    return left;
                }
                while (true) {
                    const ExprToken& token = Peek();
                    const BinaryOperator op = BinaryOf(token.kind);
                    if (op.precedence == 0 || op.precedence < minimum) {
                        return left;
                    }
                    Take();
                    const NodeResult right = ParseBinary(op.precedence + 1);
                    if (!right.has_value()) {
                        return right;
                    }
                    ExprNode node;
                    node.kind = ExprNodeKind::Binary;
                    node.operation = op.operation;
                    node.column = token.column;
                    node.first = *left;
                    node.second = *right;
                    left = Add(std::move(node));
                    if (!left.has_value()) {
                        return left;
                    }
                }
            }

            NodeResult ParseUnary() {
                if (_depth > ExprLimits::kMaxNesting) {
                    return Fail(Peek(), Format("the formula is nested deeper than {} levels", ExprLimits::kMaxNesting));
                }
                ++_depth;
                const NodeResult result = ParseUnaryBody();
                --_depth;
                return result;
            }

            NodeResult ParseUnaryBody() {
                const ExprToken& token = Peek();
                if (token.kind == ExprTokenKind::Minus || token.kind == ExprTokenKind::Bang) {
                    Take();
                    const NodeResult operand = ParseUnary();
                    if (!operand.has_value()) {
                        return operand;
                    }
                    ExprNode node;
                    node.kind = ExprNodeKind::Unary;
                    node.operation = token.kind == ExprTokenKind::Minus ? ProgramOperation::Neg : ProgramOperation::Not;
                    node.column = token.column;
                    node.first = *operand;
                    return Add(std::move(node));
                }
                NodeResult node = ParsePrimary();
                while (node.has_value() && Peek().kind == ExprTokenKind::Dot) {
                    const ExprToken& dot = Take();
                    const ExprToken& name = Peek();
                    if (name.kind != ExprTokenKind::Name) {
                        return Fail(name, Format("expected a member name after '.', found {}", DescribeToken(name)));
                    }
                    Take();
                    ExprNode member;
                    member.kind = ExprNodeKind::Member;
                    member.column = dot.column;
                    member.first = *node;
                    member.text = String(name.text);
                    node = Add(std::move(member));
                }
                return node;
            }

            NodeResult ParsePrimary() {
                const ExprToken& token = Peek();
                switch (token.kind) {
                    case ExprTokenKind::Number: {
                        Take();
                        ExprNode node;
                        node.kind = ExprNodeKind::Number;
                        node.column = token.column;
                        node.isFloat = token.isFloat;
                        node.text = String(token.text);
                        return Add(std::move(node));
                    }
                    case ExprTokenKind::String: {
                        Take();
                        ExprNode node;
                        node.kind = ExprNodeKind::String;
                        node.column = token.column;
                        node.text = String(token.text);
                        return Add(std::move(node));
                    }
                    case ExprTokenKind::Name:
                        return ParseName();
                    case ExprTokenKind::LeftParen: {
                        Take();
                        const NodeResult inner = ParseBinary(kPrecedenceOr);
                        if (!inner.has_value()) {
                            return inner;
                        }
                        const ExprToken& close = Peek();
                        if (close.kind != ExprTokenKind::RightParen) {
                            return Fail(close, Format("expected ')' to close the '(' at column {}, found {}", token.column, DescribeToken(close)));
                        }
                        Take();
                        return inner;
                    }
                    default:
                        return Fail(token, Format("expected a value, found {}", DescribeToken(token)));
                }
            }

            /// A literal `true` / `false`, a dotted name or a call of one
            NodeResult ParseName() {
                const ExprToken& first = Take();
                ExprNode node;
                node.column = first.column;
                if (first.text == "true" || first.text == "false") {
                    node.kind = ExprNodeKind::Bool;
                    node.boolValue = first.text == "true";
                    return Add(std::move(node));
                }
                String path(first.text);
                while (Peek().kind == ExprTokenKind::Dot) {
                    const ExprToken& next = Peek(1);
                    if (next.kind != ExprTokenKind::Name) {
                        return Fail(next, Format("expected a name after '.', found {}", DescribeToken(next)));
                    }
                    Take();
                    Take();
                    path += '.';
                    path.append(next.text.data(), next.text.size());
                }
                node.text = std::move(path);
                if (Peek().kind != ExprTokenKind::LeftParen) {
                    node.kind = ExprNodeKind::Name;
                    return Add(std::move(node));
                }
                const ExprToken& open = Take();
                node.kind = ExprNodeKind::Call;
                Vector<ExprNodeId> arguments;
                if (Peek().kind != ExprTokenKind::RightParen) {
                    while (true) {
                        const NodeResult argument = ParseBinary(kPrecedenceOr);
                        if (!argument.has_value()) {
                            return argument;
                        }
                        arguments.push_back(*argument);
                        if (Peek().kind != ExprTokenKind::Comma) {
                            break;
                        }
                        Take();
                    }
                }
                const ExprToken& close = Peek();
                if (close.kind != ExprTokenKind::RightParen) {
                    return Fail(close, Format("expected ',' or ')' after the arguments of '{}' (opened at column {}), found {}", node.text, open.column, DescribeToken(close)));
                }
                Take();
                // The arguments go to the list together, so a nested call's arguments do not interleave with these
                node.firstArgument = static_cast<uint32_t>(_ast.arguments.size());
                node.argumentCount = static_cast<uint32_t>(arguments.size());
                _ast.arguments.insert(_ast.arguments.end(), arguments.begin(), arguments.end());
                return Add(std::move(node));
            }

            Vector<ExprToken> _tokens;
            size_t _index = 0;
            size_t _depth = 0;
            /// `HeightOf` of each node of `_ast`
            Vector<uint32_t> _heights;
            ExprAst _ast;
        };

    }  // namespace

    std::expected<ExprAst, ProgramError> ParseExpression(StringView text) {
        auto tokens = LexExpression(text);
        if (!tokens.has_value()) {
            return std::unexpected(std::move(tokens.error()));
        }
        Parser parser(std::move(*tokens));
        return parser.Run();
    }

}  // namespace BECore
