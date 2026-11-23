#include "ScriptParser.h"

#include <BECore/Values/Programs/ExprLexer.h>

namespace BECore {

    namespace {

        constexpr StringView kIf = "if";
        constexpr StringView kElse = "else";
        constexpr StringView kEmit = "emit";
        constexpr StringView kToggle = "toggle";

        constexpr uint32_t kQuotesAroundString = 2;

        using Failure = std::unexpected<ProgramError>;
        using StatementResult = std::expected<ScriptStatementId, ProgramError>;
        using BlockResult = std::expected<ScriptBlock, ProgramError>;

        /// The 1-based column of the byte after @p token; a string is wider than its text by its quotes
        uint32_t EndColumnOf(const ExprToken& token) {
            const size_t quotes = token.kind == ExprTokenKind::String ? kQuotesAroundString : 0;
            return token.column + static_cast<uint32_t>(token.text.size() + quotes);
        }

        bool IsWord(const ExprToken& token, StringView word) {
            return token.kind == ExprTokenKind::Name && token.text == word;
        }

        class ScriptParser {
        public:
            explicit ScriptParser(ArenaVector<ExprToken> tokens) : _tokens(std::move(tokens)) {}

            std::expected<ScriptAst, ProgramError> Run() {
                Vector<ScriptStatementId> top;
                while (Peek().kind != ExprTokenKind::End) {
                    const StatementResult statement = ParseStatement(0);
                    if (!statement.has_value()) {
                        return std::unexpected(statement.error());
                    }
                    top.push_back(*statement);
                }
                _ast.root = Append(top);
                return std::move(_ast);
            }

        private:
            [[nodiscard]] const ExprToken& Peek() const {
                return _tokens[_index];
            }

            const ExprToken& Take() {
                const ExprToken& token = _tokens[_index];
                if (_index + 1 < _tokens.size()) {
                    ++_index;
                }
                return token;
            }

            /// Where a missing token belongs: right after the last one that was read
            [[nodiscard]] uint32_t AfterPrevious() const {
                return _index == 0 ? 1 : EndColumnOf(_tokens[_index - 1]);
            }

            static Failure FailAt(uint32_t column, String message) {
                return std::unexpected(ProgramError{std::move(message), column});
            }

            /// A block's statements are stored together, so the list is appended once its children (which append their own) are all parsed
            ScriptBlock Append(const Vector<ScriptStatementId>& ids) {
                const ScriptBlock block{static_cast<uint32_t>(_ast.order.size()), static_cast<uint32_t>(ids.size())};
                _ast.order.insert(_ast.order.end(), ids.begin(), ids.end());
                return block;
            }

            StatementResult Add(ScriptStatement statement) {
                if (_ast.statements.size() >= ScriptLimits::kMaxStatements) {
                    return FailAt(statement.column, Format("the script has more than {} statements", ScriptLimits::kMaxStatements));
                }
                _ast.statements.push_back(std::move(statement));
                return static_cast<ScriptStatementId>(_ast.statements.size() - 1);
            }

            /// Takes a token of @p kind or says what was expected, at the end of the previous token when @p afterPrevious
            std::expected<ExprToken, ProgramError> Expect(ExprTokenKind kind, StringView symbol, StringView context, bool afterPrevious) {
                const ExprToken& token = Peek();
                if (token.kind != kind) {
                    return FailAt(afterPrevious ? AfterPrevious() : token.column, Format("expected '{}' {}, found {}", symbol, context, DescribeToken(token)));
                }
                return Take();
            }

            /// The tokens of one expression: up to the first `;`, `{`, `}` or end of text, or (in a parenthesis) the first `)` or, with @p commaEnds, `,` that is not inside a
            /// nested parenthesis. Their text is not looked at here: the compiler compiles it as a formula and its messages name what is wrong inside
            std::expected<ScriptRange, ProgramError> ScanExpression(bool inParenthesis, bool commaEnds, StringView after) {
                const ExprToken& first = Peek();
                uint32_t depth = 0;
                uint32_t last = first.column;
                bool any = false;
                while (true) {
                    const ExprToken& token = Peek();
                    const ExprTokenKind kind = token.kind;
                    if (kind == ExprTokenKind::End || kind == ExprTokenKind::Semicolon || kind == ExprTokenKind::LeftBrace || kind == ExprTokenKind::RightBrace) {
                        break;
                    }
                    if (depth == 0 && inParenthesis && (kind == ExprTokenKind::RightParen || (commaEnds && kind == ExprTokenKind::Comma))) {
                        break;
                    }
                    if (kind == ExprTokenKind::LeftParen) {
                        ++depth;
                    } else if (kind == ExprTokenKind::RightParen && depth > 0) {
                        --depth;
                    }
                    last = EndColumnOf(token);
                    any = true;
                    Take();
                }
                if (!any) {
                    return FailAt(first.column, Format("expected an expression {}, found {}", after, DescribeToken(first)));
                }
                return ScriptRange{first.column - 1, last - 1};
            }

            /// `name` or `a.b.c`: a whole dotted path, the target of a write
            std::expected<String, ProgramError> ParsePath(StringView what) {
                const ExprToken& first = Peek();
                if (first.kind != ExprTokenKind::Name) {
                    return FailAt(first.column, Format("{} needs a field path, found {}", what, DescribeToken(first)));
                }
                String path(Take().text);
                while (Peek().kind == ExprTokenKind::Dot) {
                    Take();
                    const ExprToken& name = Peek();
                    if (name.kind != ExprTokenKind::Name) {
                        return FailAt(name.column, Format("expected a field name after '.', found {}", DescribeToken(name)));
                    }
                    path.append(".");
                    path.append(Take().text.data(), name.text.size());
                }
                return path;
            }

            std::expected<ExprToken, ProgramError> ExpectSemicolon() {
                return Expect(ExprTokenKind::Semicolon, ";", "to end the statement", true);
            }

            StatementResult ParseStatement(size_t depth) {
                const ExprToken& token = Peek();
                if (token.kind == ExprTokenKind::RightBrace) {
                    return FailAt(token.column, "unexpected '}': there is no '{' to close");
                }
                if (token.kind != ExprTokenKind::Name) {
                    return FailAt(token.column, Format("expected a statement (a field to write, 'if', 'toggle' or 'emit'), found {}", DescribeToken(token)));
                }
                if (IsWord(token, kIf)) {
                    return ParseIf(depth);
                }
                if (IsWord(token, kEmit)) {
                    return ParseEmit();
                }
                if (IsWord(token, kToggle)) {
                    return ParseToggle();
                }
                if (IsWord(token, kElse)) {
                    return FailAt(token.column, "'else' has no 'if' before it");
                }
                return ParseWrite();
            }

            /// `target = e;`, `target += e;`, `target -= e;`, `target++;`, `target--;`
            StatementResult ParseWrite() {
                ScriptStatement statement;
                statement.targetColumn = Peek().column;
                auto path = ParsePath("a statement");
                if (!path.has_value()) {
                    return std::unexpected(path.error());
                }
                statement.target = std::move(*path);
                const ExprToken& op = Peek();
                switch (op.kind) {
                    case ExprTokenKind::Equal:
                        statement.kind = ScriptStatementKind::Assign;
                        break;
                    case ExprTokenKind::PlusEqual:
                        statement.kind = ScriptStatementKind::AddAssign;
                        break;
                    case ExprTokenKind::MinusEqual:
                        statement.kind = ScriptStatementKind::SubAssign;
                        break;
                    case ExprTokenKind::PlusPlus:
                        statement.kind = ScriptStatementKind::Increment;
                        break;
                    case ExprTokenKind::MinusMinus:
                        statement.kind = ScriptStatementKind::Decrement;
                        break;
                    case ExprTokenKind::LeftParen:
                        return FailAt(op.column, Format("'{}' is a call, not a field: a statement writes a field ('{} = ...;') or emits a command ('emit {}(...);')", statement.target,
                                                        statement.target, statement.target));
                    default:
                        return FailAt(op.column, Format("expected '=', '+=', '-=', '++' or '--' after '{}', found {}", statement.target, DescribeToken(op)));
                }
                statement.column = op.column;
                const bool hasValue = statement.kind == ScriptStatementKind::Assign || statement.kind == ScriptStatementKind::AddAssign || statement.kind == ScriptStatementKind::SubAssign;
                Take();
                if (hasValue) {
                    const auto range = ScanExpression(false, false, Format("after '{}'", op.text));
                    if (!range.has_value()) {
                        return std::unexpected(range.error());
                    }
                    statement.expression = *range;
                }
                if (const auto end = ExpectSemicolon(); !end.has_value()) {
                    return std::unexpected(end.error());
                }
                return Add(std::move(statement));
            }

            StatementResult ParseToggle() {
                ScriptStatement statement;
                statement.kind = ScriptStatementKind::Toggle;
                statement.column = Take().column;
                statement.targetColumn = Peek().column;
                auto path = ParsePath("'toggle'");
                if (!path.has_value()) {
                    return std::unexpected(path.error());
                }
                statement.target = std::move(*path);
                if (const auto end = ExpectSemicolon(); !end.has_value()) {
                    return std::unexpected(end.error());
                }
                return Add(std::move(statement));
            }

            /// `emit Name(e, e, e);`
            StatementResult ParseEmit() {
                ScriptStatement statement;
                statement.kind = ScriptStatementKind::Emit;
                statement.column = Take().column;
                const ExprToken& name = Peek();
                if (name.kind != ExprTokenKind::Name) {
                    return FailAt(name.column, Format("expected a command name after 'emit', found {}", DescribeToken(name)));
                }
                statement.targetColumn = name.column;
                statement.target = String(name.text);
                Take();
                if (const auto open = Expect(ExprTokenKind::LeftParen, "(", Format("after the command name '{}'", statement.target), false); !open.has_value()) {
                    return std::unexpected(open.error());
                }
                statement.firstArgument = static_cast<uint32_t>(_ast.arguments.size());
                if (Peek().kind != ExprTokenKind::RightParen) {
                    while (true) {
                        const auto range = ScanExpression(true, true, "as an argument");
                        if (!range.has_value()) {
                            return std::unexpected(range.error());
                        }
                        _ast.arguments.push_back(*range);
                        ++statement.argumentCount;
                        if (Peek().kind != ExprTokenKind::Comma) {
                            break;
                        }
                        Take();
                    }
                }
                if (const auto close = Expect(ExprTokenKind::RightParen, ")", "to close the arguments", true); !close.has_value()) {
                    return std::unexpected(close.error());
                }
                if (const auto end = ExpectSemicolon(); !end.has_value()) {
                    return std::unexpected(end.error());
                }
                return Add(std::move(statement));
            }

            /// `{ statement* }` at @p depth blocks already open
            BlockResult ParseBlock(size_t depth, StringView context) {
                const ExprToken& open = Peek();
                if (open.kind != ExprTokenKind::LeftBrace) {
                    return FailAt(open.column, Format("expected '{{' {}, found {}", context, DescribeToken(open)));
                }
                if (depth + 1 > ScriptLimits::kMaxBlockDepth) {
                    return FailAt(open.column, Format("blocks are nested deeper than {} levels", ScriptLimits::kMaxBlockDepth));
                }
                const uint32_t openColumn = open.column;
                Take();
                Vector<ScriptStatementId> ids;
                while (Peek().kind != ExprTokenKind::RightBrace) {
                    if (Peek().kind == ExprTokenKind::End) {
                        return FailAt(openColumn, "the '{' is not closed: expected '}' before the end of the text");
                    }
                    const StatementResult statement = ParseStatement(depth + 1);
                    if (!statement.has_value()) {
                        return std::unexpected(statement.error());
                    }
                    ids.push_back(*statement);
                }
                Take();
                return Append(ids);
            }

            /// `if (e) block`, then any number of `else if (e) block` and at most one `else block`; an `else if` is the one statement of an else block, made in a loop so a long
            /// chain does not nest the parser
            StatementResult ParseIf(size_t depth) {
                Optional<ScriptStatementId> first;
                ScriptStatementId current = 0;
                while (true) {
                    ScriptStatement statement;
                    statement.kind = ScriptStatementKind::If;
                    statement.column = Take().column;
                    if (const auto open = Expect(ExprTokenKind::LeftParen, "(", "after 'if'", false); !open.has_value()) {
                        return std::unexpected(open.error());
                    }
                    const auto condition = ScanExpression(true, false, "as the condition");
                    if (!condition.has_value()) {
                        return std::unexpected(condition.error());
                    }
                    statement.expression = *condition;
                    if (const auto close = Expect(ExprTokenKind::RightParen, ")", "to close the condition", true); !close.has_value()) {
                        return std::unexpected(close.error());
                    }
                    const StatementResult added = Add(std::move(statement));
                    if (!added.has_value()) {
                        return added;
                    }
                    if (first.has_value()) {
                        _ast.statements[current].elseBlock = Append({*added});
                    } else {
                        first = *added;
                    }
                    current = *added;
                    const BlockResult body = ParseBlock(depth, "after the condition");
                    if (!body.has_value()) {
                        return std::unexpected(body.error());
                    }
                    _ast.statements[current].thenBlock = *body;
                    if (!IsWord(Peek(), kElse)) {
                        break;
                    }
                    Take();
                    _ast.statements[current].hasElse = true;
                    if (IsWord(Peek(), kIf)) {
                        continue;
                    }
                    const BlockResult otherwise = ParseBlock(depth, "after 'else'");
                    if (!otherwise.has_value()) {
                        return std::unexpected(otherwise.error());
                    }
                    _ast.statements[current].elseBlock = *otherwise;
                    break;
                }
                return *first;
            }

            ArenaVector<ExprToken> _tokens;
            size_t _index = 0;
            ScriptAst _ast;
        };

    }  // namespace

    std::expected<ScriptAst, ProgramError> ParseScript(StringView text) {
        auto tokens = LexExpression(text, LexOptions{true, ScriptLimits::kMaxTextBytes});
        if (!tokens.has_value()) {
            return std::unexpected(tokens.error());
        }
        ScriptParser parser(std::move(*tokens));
        return parser.Run();
    }

}  // namespace BECore
