// The text front end of scripts (Values/Effects/ScriptParser, ScriptAst and the statement mode of ExprLexer; Values/Effects/CLAUDE.md, "The text of a script"): the shape of every
// statement, the byte range of each expression, the errors with their columns, the limits and hostile text.

#include <BECore/Values/Effects/ScriptParser.h>
#include <BECore/Values/Programs/ExprLexer.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        String Slice(StringView text, ScriptRange range) {
            return String(text.substr(range.begin, range.end - range.begin));
        }

        String DumpBlock(const ScriptAst& ast, StringView text, ScriptBlock block);

        String DumpStatement(const ScriptAst& ast, StringView text, ScriptStatementId id) {
            const ScriptStatement& statement = ast[id];
            switch (statement.kind) {
                case ScriptStatementKind::Assign:
                    return Format("{} = {};", statement.target, Slice(text, statement.expression));
                case ScriptStatementKind::AddAssign:
                    return Format("{} += {};", statement.target, Slice(text, statement.expression));
                case ScriptStatementKind::SubAssign:
                    return Format("{} -= {};", statement.target, Slice(text, statement.expression));
                case ScriptStatementKind::Increment:
                    return Format("{}++;", statement.target);
                case ScriptStatementKind::Decrement:
                    return Format("{}--;", statement.target);
                case ScriptStatementKind::Toggle:
                    return Format("toggle {};", statement.target);
                case ScriptStatementKind::Emit: {
                    String arguments;
                    for (uint32_t i = 0; i < statement.argumentCount; ++i) {
                        arguments += (i == 0 ? "" : ", ") + Slice(text, ast.arguments[statement.firstArgument + i]);
                    }
                    return Format("emit {}({});", statement.target, arguments);
                }
                case ScriptStatementKind::If: {
                    String result = Format("if ({}) {}", Slice(text, statement.expression), DumpBlock(ast, text, statement.thenBlock));
                    if (statement.hasElse) {
                        const bool chained = statement.elseBlock.count == 1 && ast[ast.At(statement.elseBlock, 0)].kind == ScriptStatementKind::If;
                        result += chained ? " else " + DumpStatement(ast, text, ast.At(statement.elseBlock, 0)) : " else " + DumpBlock(ast, text, statement.elseBlock);
                    }
                    return result;
                }
            }
            return {};
        }

        String DumpBlock(const ScriptAst& ast, StringView text, ScriptBlock block) {
            String result = "{";
            for (uint32_t i = 0; i < block.count; ++i) {
                result += " " + DumpStatement(ast, text, ast.At(block, i));
            }
            return result + " }";
        }

        ScriptAst Parse(StringView text) {
            auto parsed = ParseScript(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed.has_value() ? String{} : parsed.error().message).c_str();
            return parsed.has_value() ? std::move(*parsed) : ScriptAst{};
        }

        String Dumped(StringView text) {
            const ScriptAst ast = Parse(text);
            return DumpBlock(ast, text, ast.root);
        }

        ProgramError ExpectError(StringView text, uint32_t column, StringView words) {
            const auto parsed = ParseScript(text);
            EXPECT_FALSE(parsed.has_value()) << String(text).c_str();
            if (parsed.has_value()) {
                return {};
            }
            EXPECT_EQ(parsed.error().column, column) << String(text).c_str() << ": " << parsed.error().message.c_str();
            EXPECT_TRUE(Contains(parsed.error().message, words)) << String(text).c_str() << ": " << parsed.error().message.c_str() << " does not say '" << String(words).c_str() << "'";
            return parsed.error();
        }

        String Repeated(StringView part, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        /// What a parse promises about any text: an error is placed inside the text, a tree points only inside the text and at statements and arguments that exist
        void ExpectSound(StringView text) {
            const auto parsed = ParseScript(text);
            if (!parsed.has_value()) {
                EXPECT_GE(parsed.error().column, 1u);
                EXPECT_LE(parsed.error().column, text.size() + 1) << parsed.error().message.c_str();
                EXPECT_FALSE(parsed.error().message.empty());
                return;
            }
            const ScriptAst& ast = *parsed;
            EXPECT_LE(ast.statements.size(), ScriptLimits::kMaxStatements);
            for (const ScriptStatementId id : ast.order) {
                EXPECT_LT(id, ast.statements.size());
            }
            for (const ScriptRange& range : ast.arguments) {
                EXPECT_LT(range.begin, range.end);
                EXPECT_LE(range.end, text.size());
            }
            for (const ScriptStatement& statement : ast.statements) {
                EXPECT_LE(statement.expression.end, text.size());
                EXPECT_LE(statement.expression.begin, statement.expression.end);
                EXPECT_LE(statement.thenBlock.first + statement.thenBlock.count, ast.order.size());
                EXPECT_LE(statement.elseBlock.first + statement.elseBlock.count, ast.order.size());
                EXPECT_LE(statement.firstArgument + statement.argumentCount, ast.arguments.size());
                EXPECT_LE(statement.column, text.size());
            }
        }

    }  // namespace

    TEST(ScriptParserTest, EveryStatementFormIsRead) {
        EXPECT_EQ(Dumped("cell.timer = 1;"), "{ cell.timer = 1; }");
        EXPECT_EQ(Dumped("cell.timer=cell.timer+1;"), "{ cell.timer = cell.timer+1; }");
        EXPECT_EQ(Dumped("a.b += 2 * c;"), "{ a.b += 2 * c; }");
        EXPECT_EQ(Dumped("a -= 1;"), "{ a -= 1; }");
        EXPECT_EQ(Dumped("a.b++;"), "{ a.b++; }");
        EXPECT_EQ(Dumped("a--;"), "{ a--; }");
        EXPECT_EQ(Dumped("toggle cell.open;"), "{ toggle cell.open; }");
        EXPECT_EQ(Dumped("emit Open();"), "{ emit Open(); }");
        EXPECT_EQ(Dumped("emit Open(1);"), "{ emit Open(1); }");
        EXPECT_EQ(Dumped("emit Move(cell.index, min(a, b), 'x');"), "{ emit Move(cell.index, min(a, b), 'x'); }");
        EXPECT_EQ(Dumped("a = 1; b = 2; toggle c;"), "{ a = 1; b = 2; toggle c; }");
    }

    TEST(ScriptParserTest, AnEmptyScriptIsValidAndHasNoStatements) {
        for (const StringView text : {StringView{""}, StringView{"   "}, StringView{"\n\t \r\n"}}) {
            const ScriptAst ast = Parse(text);
            EXPECT_TRUE(ast.statements.empty());
            EXPECT_EQ(ast.root.count, 0u);
        }
    }

    TEST(ScriptParserTest, IfElseIfElseChainsAreNestedBlocks) {
        EXPECT_EQ(Dumped("if (a) { x = 1; }"), "{ if (a) { x = 1; } }");
        EXPECT_EQ(Dumped("if (a) { } else { y = 2; }"), "{ if (a) { } else { y = 2; } }");
        EXPECT_EQ(Dumped("if (a) { x = 1; } else if (b) { y = 2; } else { z = 3; }"), "{ if (a) { x = 1; } else if (b) { y = 2; } else { z = 3; } }");
        EXPECT_EQ(Dumped("if (a) { if (b) { if (c) { x = 1; } else { x = 2; } } y = 3; } z = 4;"), "{ if (a) { if (b) { if (c) { x = 1; } else { x = 2; } } y = 3; } z = 4; }");
        const String chained = Dumped("if (a) { } else if (b) { }");
        EXPECT_EQ(Dumped("if (a) { } else { if (b) { } }"), chained) << "an else if is an else block of one if";
    }

    TEST(ScriptParserTest, TheRangesAreTheExpressionsAndTheColumnsAreTheOperators) {
        constexpr StringView text = "cell.timer -= 1 + cell.step;\nif ( cell.open && (a || b) ) { emit Act(cell.index , f(1, 2)) ; }";
        const ScriptAst ast = Parse(text);
        ASSERT_EQ(ast.root.count, 2u);

        const ScriptStatement& subtract = ast[ast.At(ast.root, 0)];
        EXPECT_EQ(subtract.kind, ScriptStatementKind::SubAssign);
        EXPECT_EQ(subtract.target, "cell.timer");
        EXPECT_EQ(subtract.targetColumn, 1u);
        EXPECT_EQ(subtract.column, 12u) << "the operator";
        EXPECT_EQ(Slice(text, subtract.expression), "1 + cell.step");

        const ScriptStatement& branch = ast[ast.At(ast.root, 1)];
        EXPECT_EQ(branch.kind, ScriptStatementKind::If);
        EXPECT_EQ(branch.column, 30u) << "the keyword";
        EXPECT_EQ(Slice(text, branch.expression), "cell.open && (a || b)");

        const ScriptStatement& emit = ast[ast.At(branch.thenBlock, 0)];
        EXPECT_EQ(emit.kind, ScriptStatementKind::Emit);
        EXPECT_EQ(emit.target, "Act");
        EXPECT_EQ(emit.targetColumn, static_cast<uint32_t>(text.find("Act") + 1));
        ASSERT_EQ(emit.argumentCount, 2u) << "a comma inside a call does not split";
        EXPECT_EQ(Slice(text, ast.arguments[emit.firstArgument]), "cell.index");
        EXPECT_EQ(Slice(text, ast.arguments[emit.firstArgument + 1]), "f(1, 2)");
    }

    TEST(ScriptParserTest, AStringKeepsItsQuotesInTheRangeAndItsEndColumn) {
        constexpr StringView text = "emit Say('a b;{}', 'c');";
        const ScriptAst ast = Parse(text);
        const ScriptStatement& emit = ast[ast.At(ast.root, 0)];
        ASSERT_EQ(emit.argumentCount, 2u);
        EXPECT_EQ(Slice(text, ast.arguments[0]), "'a b;{}'");
        EXPECT_EQ(Slice(text, ast.arguments[1]), "'c'");
    }

    TEST(ScriptParserTest, AnExpressionIsNotCheckedHere) {
        EXPECT_EQ(Dumped("x = 1 = 2;"), "{ x = 1 = 2; }");
        EXPECT_EQ(Dumped("x = 1 2;"), "{ x = 1 2; }");
        EXPECT_EQ(Dumped("x = (1 + ;"), "{ x = (1 +; }") << "the compiler names the open parenthesis";
        EXPECT_EQ(Dumped("x = a--b;"), "{ x = a--b; }");
        EXPECT_EQ(Dumped("emit A(1 2, 3);"), "{ emit A(1 2, 3); }");
    }

    TEST(ScriptParserTest, TheKeywordsAreKeywordsAtTheStartOfAStatementOnly) {
        EXPECT_EQ(Dumped("x = toggle;"), "{ x = toggle; }");
        EXPECT_EQ(Dumped("x = if + else + emit;"), "{ x = if + else + emit; }");
        EXPECT_EQ(Dumped("if (toggle.on) { a = emit.x + else.y; }"), "{ if (toggle.on) { a = emit.x + else.y; } }");
        ExpectError("toggle.x = 1;", 7, "'toggle' needs a field path");
    }

    TEST(ScriptParserTest, EveryErrorNamesWhatWasExpectedAndWhereItWasFound) {
        ExpectError("x = 1", 6, "expected ';'");
        ExpectError("x = 1 }", 6, "expected ';'");
        ExpectError("x++", 4, "expected ';'");
        ExpectError("toggle x", 9, "expected ';'");
        ExpectError("emit A(1)", 10, "expected ';'");
        ExpectError("a = 1; b = 2", 13, "expected ';'");
        ExpectError("x = ;", 5, "expected an expression after '='");
        ExpectError("x += ;", 6, "expected an expression after '+='");
        ExpectError("x -= }", 6, "expected an expression after '-='");
        ExpectError("if x > 1 { }", 4, "expected '(' after 'if'");
        ExpectError("if (x > 1 { }", 10, "expected ')' to close the condition");
        ExpectError("if () { }", 5, "expected an expression as the condition");
        ExpectError("if (x) y = 1;", 8, "expected '{' after the condition");
        ExpectError("if (x) { } else y = 1;", 17, "expected '{' after 'else'");
        ExpectError("if (x) { } else", 16, "expected '{' after 'else', found the end of the text");
        ExpectError("else { }", 1, "'else' has no 'if' before it");
        ExpectError("if (x) { } else { } else { }", 21, "'else' has no 'if' before it");
        ExpectError("if (x) { y = 1;", 8, "the '{' is not closed");
        ExpectError("if (x) { if (y) { z = 1; }", 8, "the '{' is not closed");
        ExpectError("}", 1, "there is no '{' to close");
        ExpectError("x = 1; }", 8, "there is no '{' to close");
        ExpectError("toggle ;", 8, "'toggle' needs a field path");
        ExpectError("toggle 1;", 8, "'toggle' needs a field path");
        ExpectError("5 = 1;", 1, "expected a statement");
        ExpectError("(x) = 1;", 1, "expected a statement");
        ExpectError("a.b(c) = 1;", 4, "'a.b' is a call, not a field");
        ExpectError("a. = 1;", 4, "expected a field name after '.'");
        ExpectError("a.1 = 1;", 3, "expected a field name after '.'");
        ExpectError("x y = 1;", 3, "expected '=', '+=', '-=', '++' or '--' after 'x'");
        ExpectError("x;", 2, "after 'x', found ';'");
        ExpectError("x == 1;", 3, "found '=='");
        ExpectError("x", 2, "found the end of the text");
        ExpectError("emit ;", 6, "expected a command name after 'emit'");
        ExpectError("emit A;", 7, "expected '(' after the command name 'A'");
        ExpectError("emit a.b(1);", 7, "expected '(' after the command name 'a', found '.'");
        ExpectError("emit A(1,);", 10, "expected an expression as an argument");
        ExpectError("emit A(,1);", 8, "expected an expression as an argument");
        ExpectError("emit A(1;", 9, "expected ')' to close the arguments");
        ExpectError("emit A(1", 9, "expected ')' to close the arguments");
        ExpectError("x = 1 # 2;", 7, "unexpected '#'");
        ExpectError("x = 'abc;", 5, "unterminated string");
        ExpectError("x = [1];", 5, "'[' is not allowed");
    }

    TEST(ScriptParserTest, TheTextLimitIsExactAndTheMessageSaysScript) {
        const String padding = Repeated(" ", ScriptLimits::kMaxTextBytes - 5);
        const String fits = "a = 1;" + padding.substr(1);
        ASSERT_EQ(fits.size(), ScriptLimits::kMaxTextBytes);
        EXPECT_EQ(Dumped(fits), "{ a = 1; }");
        const ProgramError error = ExpectError(fits + " ", ScriptLimits::kMaxTextBytes + 1, "the script is longer than 16384 bytes");
        EXPECT_FALSE(Contains(error.message, "formula"));
    }

    TEST(ScriptParserTest, TheStatementLimitCountsEveryBlockAndIsExact) {
        const String fits = Repeated("a = 1;", ScriptLimits::kMaxStatements);
        EXPECT_EQ(Parse(fits).statements.size(), ScriptLimits::kMaxStatements);
        ExpectError(fits + "a = 1;", static_cast<uint32_t>(fits.size() + 3), "more than 256 statements");

        // 128 `if` and their 128 bodies: the nested statements count with the outer ones
        String nested;
        for (size_t i = 0; i < ScriptLimits::kMaxStatements / 2; ++i) {
            nested += "if (a) { b = 1; }";
        }
        EXPECT_EQ(Parse(nested).statements.size(), ScriptLimits::kMaxStatements);
        ExpectError(nested + "c = 1;", static_cast<uint32_t>(nested.size() + 3), "more than 256 statements");
    }

    TEST(ScriptParserTest, TheBlockDepthLimitIsExact) {
        const auto nest = [](size_t depth) {
            String text;
            for (size_t i = 0; i < depth; ++i) {
                text += "if (a) { ";
            }
            text += "x = 1;";
            for (size_t i = 0; i < depth; ++i) {
                text += " }";
            }
            return text;
        };
        EXPECT_EQ(Parse(nest(ScriptLimits::kMaxBlockDepth)).statements.size(), ScriptLimits::kMaxBlockDepth + 1);
        const String over = nest(ScriptLimits::kMaxBlockDepth + 1);
        ExpectError(over, static_cast<uint32_t>(ScriptLimits::kMaxBlockDepth * 9 + 8), "nested deeper than 8 levels");
    }

    TEST(ScriptParserTest, AnElseIfChainDoesNotNestTheBlockDepthOrTheParser) {
        String chain = "if (a0) { }";
        for (size_t i = 1; i < ScriptLimits::kMaxStatements; ++i) {
            chain += Format(" else if (a{}) {{ }}", i);
        }
        const ScriptAst ast = Parse(chain);
        EXPECT_EQ(ast.statements.size(), ScriptLimits::kMaxStatements);
        ExpectError(chain + " else if (z) { }", static_cast<uint32_t>(chain.size() + 7), "more than 256 statements");
    }

    TEST(ScriptParserTest, HostileTextIsAnErrorOrASoundTreeAndNeverACrash) {
        constexpr size_t kHuge = 100000;
        ExpectSound(Repeated("{", kHuge));
        ExpectSound(Repeated("}", kHuge));
        ExpectSound(Repeated(";", kHuge));
        ExpectSound(Repeated("if (a) { ", kHuge / 9));
        ExpectSound(Repeated("(", kHuge));
        ExpectSound("x = " + Repeated("(", ScriptLimits::kMaxTextBytes - 8) + ";");
        ExpectSound("x = " + Repeated("1 + ", 4000) + "1;");
        ExpectSound(Repeated("emit A(", 2000) + Repeated(")", 2000));
        ExpectSound("emit A(" + Repeated("1,", 8000) + "1);");
        ExpectSound(Repeated("else ", 3000));
        ExpectSound(Repeated("a.", 8000));
        ExpectSound(Repeated("a++ ", 4000));
        ExpectSound(Repeated("+=", 8000));
        ExpectSound(Repeated("'", kHuge));

        for (int byte = 0; byte < 256; ++byte) {
            const String one(1, static_cast<char>(byte));
            ExpectSound(one);
            ExpectSound("x = 1" + one + ";");
            ExpectSound("if (a) { " + one + " }");
            ExpectSound("emit A(" + one + ");");
        }
    }

    TEST(ScriptParserTest, MutatedScriptsAreAnErrorOrASoundTree) {
        const StringView good =
            "if (cell.timer > 0) { cell.timer -= 1; if (cell.timer == 0) { cell.open = !cell.open; emit Action(Open, cell.index); } } else if (b.mask != c.mask) { c.mask = f(c.mask, b.mask); "
            "toggle c.flag; c.count++; }";
        EXPECT_EQ(Parse(good).statements.size(), 9u);

        uint32_t state = 0x1234ABCDu;
        const auto next = [&state](uint32_t bound) {
            state = state * 1664525u + 1013904223u;
            return (state >> 8) % bound;
        };
        constexpr StringView kFragments[] = {"{", "}", ";", "(", ")", ",", "=", "+=", "--", "++", "else", "if", "emit", "toggle", "'", "."};
        constexpr size_t kCases = 4000;
        for (size_t i = 0; i < kCases; ++i) {
            String text(good);
            const uint32_t edits = 1 + next(4);
            for (uint32_t e = 0; e < edits && !text.empty(); ++e) {
                const size_t at = next(static_cast<uint32_t>(text.size()));
                switch (next(4)) {
                    case 0:
                        text[at] = static_cast<char>(next(256));
                        break;
                    case 1:
                        text.erase(at, 1 + next(8));
                        break;
                    case 2: {
                        const StringView fragment = kFragments[next(static_cast<uint32_t>(std::size(kFragments)))];
                        text.insert(at, fragment.data(), fragment.size());
                        break;
                    }
                    default:
                        text.resize(at);
                        break;
                }
            }
            ExpectSound(text);
        }
    }

    TEST(ScriptParserTest, TheFormulaModeOfTheLexerRefusesTheScriptBytesAsBefore) {
        const auto refused = [](StringView text, uint32_t column, StringView words) {
            const auto tokens = LexExpression(text);
            ASSERT_FALSE(tokens.has_value()) << String(text).c_str();
            EXPECT_EQ(tokens.error().column, column) << String(text).c_str();
            EXPECT_TRUE(Contains(tokens.error().message, words)) << tokens.error().message.c_str();
        };
        refused("a;", 2, "unexpected ';'");
        refused("{", 1, "unexpected '{'");
        refused("a }", 3, "unexpected '}'");
        refused("a = 1", 3, "the comparison is '=='");

        refused("a+=1", 3, "the comparison is '=='");

        const auto signs = LexExpression("a--b");
        ASSERT_TRUE(signs.has_value());
        EXPECT_EQ(signs->size(), 5u) << "a formula has no `--`: it is a minus and a negation";
    }

    TEST(ScriptParserTest, TheStatementModeAddsTheScriptTokens) {
        const auto tokens = LexExpression("{ } ; = += -= ++ -- == + - a", LexOptions{true});
        ASSERT_TRUE(tokens.has_value()) << tokens.error().message.c_str();
        const ExprTokenKind expected[] = {ExprTokenKind::LeftBrace,  ExprTokenKind::RightBrace, ExprTokenKind::Semicolon,  ExprTokenKind::Equal,      ExprTokenKind::PlusEqual,
                                          ExprTokenKind::MinusEqual, ExprTokenKind::PlusPlus,   ExprTokenKind::MinusMinus, ExprTokenKind::EqualEqual, ExprTokenKind::Plus,
                                          ExprTokenKind::Minus,      ExprTokenKind::Name,       ExprTokenKind::End};
        ASSERT_EQ(tokens->size(), std::size(expected));
        for (size_t i = 0; i < std::size(expected); ++i) {
            EXPECT_EQ((*tokens)[i].kind, expected[i]) << i;
        }
    }

}  // namespace BECore::Tests
