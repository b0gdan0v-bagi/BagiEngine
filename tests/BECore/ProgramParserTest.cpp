// The text front end of formulas (Values/Programs/ExprLexer, ExprParser, ExprAst): the grammar, the printer, the errors with their columns and the limits.

#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ExprAst Parse(StringView text) {
            auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed.has_value() ? String{} : parsed.error().message).c_str();
            return parsed.has_value() ? std::move(*parsed) : ExprAst{};
        }

        String Printed(StringView text) {
            return PrintExpression(Parse(text));
        }

        /// The tree without its columns: the same shape written differently has the same nodes
        bool SameTree(const ExprAst& a, const ExprAst& b) {
            if (a.nodes.size() != b.nodes.size() || a.root != b.root || a.arguments != b.arguments) {
                return false;
            }
            for (size_t i = 0; i < a.nodes.size(); ++i) {
                const ExprNode& x = a.nodes[i];
                const ExprNode& y = b.nodes[i];
                if (x.kind != y.kind || x.operation != y.operation || x.isFloat != y.isFloat || x.boolValue != y.boolValue || x.first != y.first || x.second != y.second ||
                    x.firstArgument != y.firstArgument || x.argumentCount != y.argumentCount || x.text != y.text) {
                    return false;
                }
            }
            return true;
        }

        ProgramError ExpectError(StringView text, uint32_t column, StringView words) {
            const auto parsed = ParseExpression(text);
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

    }  // namespace

    TEST(ProgramParserTest, EveryOperatorParsesToItsOperation) {
        struct Row {
            StringView symbol;
            ProgramOperation operation;
        };
        const Row rows[] = {{"||", ProgramOperation::Or}, {"&&", ProgramOperation::And}, {"==", ProgramOperation::Eq}, {"!=", ProgramOperation::Ne}, {"<", ProgramOperation::Lt},
                            {"<=", ProgramOperation::Le}, {">", ProgramOperation::Gt},   {">=", ProgramOperation::Ge}, {"+", ProgramOperation::Add}, {"-", ProgramOperation::Sub},
                            {"*", ProgramOperation::Mul}, {"/", ProgramOperation::Div},  {"%", ProgramOperation::Mod}};
        for (const Row& row : rows) {
            const String text = Format("a {} b", row.symbol);
            const ExprAst ast = Parse(text);
            ASSERT_EQ(ast.nodes.size(), 3U) << text.c_str();
            EXPECT_EQ(ast[ast.root].kind, ExprNodeKind::Binary) << text.c_str();
            EXPECT_EQ(ast[ast.root].operation, row.operation) << text.c_str();
            EXPECT_EQ(PrintExpression(ast), text) << "the printer writes the symbol back";
            EXPECT_EQ(OperatorSymbol(row.operation), row.symbol);
        }
    }

    TEST(ProgramParserTest, PrecedenceAndAssociativityAreTheTableSaysAsFullyParenthesizedText) {
        struct Row {
            StringView text;
            StringView expected;
        };
        const Row rows[] = {
            {"a || b && c", "a || (b && c)"},
            {"a && b || c", "(a && b) || c"},
            {"a && b == c", "a && (b == c)"},
            {"a == b < c", "a == (b < c)"},
            {"a < b + c", "a < (b + c)"},
            {"a + b * c", "a + (b * c)"},
            {"a * b + c", "(a * b) + c"},
            {"a - b - c", "(a - b) - c"},
            {"a / b / c", "(a / b) / c"},
            {"a % b * c", "(a % b) * c"},
            {"a || b || c", "(a || b) || c"},
            {"a && b && c", "(a && b) && c"},
            {"a == b == c", "(a == b) == c"},
            {"a < b <= c", "(a < b) <= c"},
            {"(a + b) * c", "(a + b) * c"},
            {"a * (b + c)", "a * (b + c)"},
            {"a - (b - c)", "a - (b - c)"},
            {"((a))", "a"},
            {"-a * b", "-a * b"},
            {"-(a * b)", "-(a * b)"},
            {"!a && b", "!a && b"},
            {"!a == b", "!a == b"},
            {"a + b == c * d", "(a + b) == (c * d)"},
            {"a < b == c > d", "(a < b) == (c > d)"},
            {"a || b && c == d + e * f", "a || (b && (c == (d + (e * f))))"},
            {"-f(x) + 1", "-f(x) + 1"},
        };
        for (const Row& row : rows) {
            EXPECT_EQ(Printed(row.text), row.expected) << String(row.text).c_str();
        }
    }

    TEST(ProgramParserTest, UnaryMinusBeforeALiteralIsAnOperatorAboveTheNumber) {
        const ExprAst ast = Parse("-5");
        ASSERT_EQ(ast.nodes.size(), 2U);
        EXPECT_EQ(ast[ast.root].kind, ExprNodeKind::Unary);
        EXPECT_EQ(ast[ast.root].operation, ProgramOperation::Neg);
        EXPECT_EQ(ast[ast[ast.root].first].kind, ExprNodeKind::Number);
        EXPECT_EQ(ast[ast[ast.root].first].text, "5");
        EXPECT_EQ(PrintExpression(ast), "-5");
        EXPECT_EQ(Printed("- -5"), "-(-5)");
        EXPECT_EQ(Printed("a - -5"), "a - -5");
        EXPECT_EQ(Printed("!!a"), "!(!a)");
        EXPECT_EQ(Parse("!a")[Parse("!a").root].operation, ProgramOperation::Not);
    }

    TEST(ProgramParserTest, NumbersKeepTheirTextAndTellFloatsFromIntegers) {
        struct Row {
            StringView text;
            bool isFloat;
        };
        const Row rows[] = {{"12", false}, {"0", false}, {"1.5", true}, {".5", true}, {"1e3", true}, {"1E3", true}, {"1.5e-3", true}, {"2e+8", true}, {"007", false}, {"3.0", true}};
        for (const Row& row : rows) {
            const ExprAst ast = Parse(row.text);
            ASSERT_EQ(ast.nodes.size(), 1U) << String(row.text).c_str();
            EXPECT_EQ(ast[ast.root].kind, ExprNodeKind::Number);
            EXPECT_EQ(ast[ast.root].isFloat, row.isFloat) << String(row.text).c_str();
            EXPECT_EQ(ast[ast.root].text, row.text) << "the text is kept as written";
        }
        EXPECT_EQ(Printed("1 + .5"), "1 + .5");
        EXPECT_EQ(Printed("(.5)"), ".5");
        EXPECT_EQ(Printed("x*.5"), "x * .5");
    }

    TEST(ProgramParserTest, BoolLiteralsAreNotNames) {
        const ExprAst yes = Parse("true");
        EXPECT_EQ(yes[yes.root].kind, ExprNodeKind::Bool);
        EXPECT_TRUE(yes[yes.root].boolValue);
        const ExprAst no = Parse("false");
        EXPECT_EQ(no[no.root].kind, ExprNodeKind::Bool);
        EXPECT_FALSE(no[no.root].boolValue);
        EXPECT_EQ(Printed("true && !false"), "true && !false");
        const ExprAst name = Parse("trueish");
        EXPECT_EQ(name[name.root].kind, ExprNodeKind::Name);
    }

    TEST(ProgramParserTest, AFunctionCallHasZeroOneOrManyArguments) {
        const ExprAst none = Parse("f()");
        EXPECT_EQ(none[none.root].kind, ExprNodeKind::Call);
        EXPECT_EQ(none[none.root].text, "f");
        EXPECT_EQ(none[none.root].argumentCount, 0U);

        const ExprAst one = Parse("abs(x)");
        EXPECT_EQ(one[one.root].argumentCount, 1U);

        const ExprAst many = Parse("clamp(a + 1, 0, min(b, c))");
        ASSERT_EQ(many[many.root].kind, ExprNodeKind::Call);
        ASSERT_EQ(many[many.root].argumentCount, 3U);
        EXPECT_EQ(many[many.arguments[many[many.root].firstArgument]].kind, ExprNodeKind::Binary);
        EXPECT_EQ(many[many.arguments[many[many.root].firstArgument + 2]].kind, ExprNodeKind::Call);
        EXPECT_EQ(PrintExpression(many), "clamp(a + 1, 0, min(b, c))");
        EXPECT_EQ(Printed("f(  1 ,2,   g( ) )"), "f(1, 2, g())");
    }

    TEST(ProgramParserTest, ADottedNameIsOneNameAndACallOfADottedNameIsACall) {
        const ExprAst path = Parse("a.b.c");
        ASSERT_EQ(path.nodes.size(), 1U);
        EXPECT_EQ(path[path.root].kind, ExprNodeKind::Name);
        EXPECT_EQ(path[path.root].text, "a.b.c");

        const ExprAst call = Parse("a.b(c)");
        EXPECT_EQ(call[call.root].kind, ExprNodeKind::Call);
        EXPECT_EQ(call[call.root].text, "a.b");
        EXPECT_EQ(call[call.root].argumentCount, 1U);
        EXPECT_EQ(PrintExpression(call), "a.b(c)");

        EXPECT_EQ(Printed("caster.span.low + cast.level"), "caster.span.low + cast.level");
    }

    TEST(ProgramParserTest, AMemberOnAnExpressionThatIsNoNameIsAMember) {
        const ExprAst ast = Parse("f(x).y.z");
        ASSERT_EQ(ast[ast.root].kind, ExprNodeKind::Member);
        EXPECT_EQ(ast[ast.root].text, "z");
        EXPECT_EQ(ast[ast[ast.root].first].kind, ExprNodeKind::Member);
        EXPECT_EQ(ast[ast[ast[ast.root].first].first].kind, ExprNodeKind::Call);
        EXPECT_EQ(PrintExpression(ast), "f(x).y.z");

        const ExprAst parenthesized = Parse("(a).b");
        EXPECT_EQ(parenthesized[parenthesized.root].kind, ExprNodeKind::Member) << "parentheses stop a name from taking the member";
        EXPECT_EQ(PrintExpression(parenthesized), "(a).b");
        EXPECT_EQ(Printed("(a + b).x"), "(a + b).x");
        EXPECT_EQ(Printed("(-a).x"), "(-a).x");
    }

    TEST(ProgramParserTest, WhitespaceAndTabsAreOnlySeparators) {
        EXPECT_EQ(Printed("  a\t+\tb  "), "a + b");
        EXPECT_EQ(Printed("a+b*c"), "a + (b * c)");
        EXPECT_EQ(Printed("a\n+\r\nb"), "a + b");
        EXPECT_EQ(Printed("a&&b||!c"), "(a && b) || !c");
        EXPECT_EQ(Printed("a<=b"), "a <= b");
        EXPECT_EQ(Printed("a != b"), "a != b");
    }

    TEST(ProgramParserTest, APrintedTextParsesToTheSameTreeAndPrintsAgain) {
        const StringView texts[] = {"caster.attack * (100 + cast.level * 10) / 100",
                                    "a || b && c == d + e * f",
                                    "-(-5) - - 3",
                                    "!(a && b) || c",
                                    "clamp(x, -1.5, f(y).z + .25e2)",
                                    "((a)).b",
                                    "min(1, max(2, 3)) % 4",
                                    "select(a < b, 1, 2) >= 3 == true",
                                    "a - (b - (c - d))"};
        for (const StringView text : texts) {
            const ExprAst first = Parse(text);
            const String printed = PrintExpression(first);
            const ExprAst second = Parse(printed);
            EXPECT_TRUE(SameTree(first, second)) << String(text).c_str() << " -> " << printed.c_str();
            EXPECT_EQ(PrintExpression(second), printed);
        }
    }

    TEST(ProgramParserTest, ColumnsAreOneBasedBytesOfTheNodes) {
        const ExprAst ast = Parse("ab + cd.e");
        EXPECT_EQ(ast[ast.root].column, 4U) << "a binary node is at its operator";
        EXPECT_EQ(ast[ast[ast.root].first].column, 1U);
        EXPECT_EQ(ast[ast[ast.root].second].column, 6U);
        const ExprAst sign = Parse("  -x");
        EXPECT_EQ(sign[sign.root].column, 3U);
    }

    TEST(ProgramParserTest, ErrorsNameWhatWasExpectedAndFoundAndWhere) {
        ExpectError("1 $ 2", 3, "unexpected '$'");
        ExpectError("(1 + 2", 7, "expected ')'");
        EXPECT_TRUE(Contains(ExpectError("(1 + 2", 7, "end of the text").message, "column 1"));
        ExpectError("1 + 2)", 6, "unexpected ')'");
        ExpectError("1 +", 4, "expected a value, found the end of the text");
        ExpectError("*1", 1, "expected a value, found '*'");
        ExpectError("(1 + )", 6, "expected a value, found ')'");
        ExpectError("1 2", 3, "expected an operator or the end of the formula, found '2'");
        ExpectError("f(1 2)", 5, "expected ',' or ')' after the arguments of 'f'");
        ExpectError("f(1,)", 5, "expected a value, found ')'");
        ExpectError("f(1", 4, "found the end of the text");
        ExpectError("a.", 3, "expected a name after '.'");
        ExpectError("a.5", 3, "expected a name after '.', found '5'");
        ExpectError("f(x).", 6, "expected a member name after '.'");
        ExpectError("a = 1", 3, "'=='");
        ExpectError("a & b", 3, "'&&'");
        ExpectError("a | b", 3, "'||'");
        ExpectError("a # b", 3, "unexpected '#'");
        ExpectError("true(1)", 5, "expected an operator");
    }

    TEST(ProgramParserTest, MalformedNumbersAreErrorsAtTheBadByte) {
        ExpectError("1..2", 3, "expected a digit after the decimal point, found '.'");
        ExpectError("1.", 3, "expected a digit after the decimal point, found the end of the text");
        ExpectError("1.x", 3, "found 'x'");
        ExpectError("1e", 3, "expected a digit in the exponent");
        ExpectError("1e+", 4, "expected a digit in the exponent");
        ExpectError("1e-x", 4, "expected a digit in the exponent, found 'x'");
        ExpectError("12abc", 3, "after the number");
        ExpectError("1 + 3f", 6, "after the number");
    }

    TEST(ProgramParserTest, AnEmptyTextIsAnError) {
        ExpectError("", 1, "empty");
        ExpectError("   \t ", 1, "empty");
    }

    TEST(ProgramParserTest, AnIndexIsRefusedAndTheMessageSaysAPathMustBeFixed) {
        const ProgramError error = ExpectError("caster.buffs[0]", 13, "'[' is not allowed");
        EXPECT_TRUE(Contains(error.message, "fixed field")) << error.message.c_str();
        ExpectError("a[\"k\"]", 2, "'['");
    }

    TEST(ProgramParserTest, TheLimitsAreErrorsThatNameThem) {
        EXPECT_TRUE(ParseExpression(Repeated("a", ExprLimits::kMaxTextBytes)).has_value());
        const ProgramError longText = ExpectError(Repeated("a", ExprLimits::kMaxTextBytes + 1), ExprLimits::kMaxTextBytes + 1, "longer than 4096 bytes");
        EXPECT_FALSE(longText.message.empty());

        EXPECT_TRUE(ParseExpression(Repeated("(", ExprLimits::kMaxNesting) + "1" + Repeated(")", ExprLimits::kMaxNesting)).has_value());
        ExpectError(Repeated("(", ExprLimits::kMaxNesting + 1) + "1" + Repeated(")", ExprLimits::kMaxNesting + 1), ExprLimits::kMaxNesting + 2, "nested deeper than 64 levels");
        EXPECT_TRUE(ParseExpression(Repeated("-", ExprLimits::kMaxTreeHeight) + "1").has_value());
        ExpectError(Repeated("-", ExprLimits::kMaxNesting + 1) + "1", ExprLimits::kMaxNesting + 2, "nested deeper than 64 levels");
        const String calls = Repeated("f(", ExprLimits::kMaxNesting + 1) + "1" + Repeated(")", ExprLimits::kMaxNesting + 1);
        ExpectError(calls, 2 * (ExprLimits::kMaxNesting + 1) + 1, "nested deeper");

        // A call of k sums `1 + 1` has 3k + 1 nodes and is two levels high
        const size_t fits = (ExprLimits::kMaxNodes - 1) / 3;
        const auto sums = [](size_t count) { return "f(" + Repeated("1 + 1, ", count - 1) + "1 + 1)"; };
        EXPECT_EQ(Parse(sums(fits)).nodes.size(), ExprLimits::kMaxNodes - 1);
        const auto parsed = ParseExpression(sums(fits + 1));
        ASSERT_FALSE(parsed.has_value());
        EXPECT_TRUE(Contains(parsed.error().message, "more than 512 parts")) << parsed.error().message.c_str();
        EXPECT_GT(parsed.error().column, 0U);
    }

    TEST(ProgramParserTest, ATreeHigherThanTheHeightLimitIsRefusedWhateverMakesItHigh) {
        constexpr size_t kLevels = ExprLimits::kMaxTreeHeight;
        // A left-nested chain is a loop of the parser but a tree as high as it is long: n operators are n levels
        const auto chain = [](size_t operators) { return "a" + Repeated(" + a", operators); };
        EXPECT_TRUE(ParseExpression(chain(kLevels)).has_value());
        const auto longChain = ParseExpression(chain(kLevels + 1));
        ASSERT_FALSE(longChain.has_value());
        EXPECT_TRUE(Contains(longChain.error().message, "more than 32 levels high")) << longChain.error().message.c_str();
        EXPECT_EQ(longChain.error().column, 4 * (kLevels + 1) - 1);

        const auto members = [](size_t count) { return "(a)" + Repeated(".x", count); };
        EXPECT_TRUE(ParseExpression(members(kLevels)).has_value());
        EXPECT_FALSE(ParseExpression(members(kLevels + 1)).has_value());

        // Parentheses add no node, so a chain inside them is as high as outside
        EXPECT_FALSE(ParseExpression("(" + chain(kLevels) + ") + a").has_value());
        // The calls, the unary operators and the operators of one expression add up
        EXPECT_FALSE(ParseExpression("f(" + chain(kLevels) + ")").has_value());
        EXPECT_FALSE(ParseExpression("-(" + chain(kLevels) + ")").has_value());
        const auto unary = ParseExpression(Repeated("-", kLevels + 1) + "1");
        ASSERT_FALSE(unary.has_value());
        EXPECT_TRUE(Contains(unary.error().message, "more than 32 levels high")) << unary.error().message.c_str();
        EXPECT_EQ(unary.error().column, 1U);
    }

    TEST(ProgramParserTest, AStringIsAnOperandWithItsTextBetweenTheQuotes) {
        const ExprAst ast = Parse("a == 'Fire'");
        ASSERT_EQ(ast.nodes.size(), 3U);
        EXPECT_EQ(ast[ast[ast.root].second].kind, ExprNodeKind::String);
        EXPECT_EQ(ast[ast[ast.root].second].text, "Fire");
        EXPECT_EQ(ast[ast[ast.root].second].column, 6U) << "the column is the opening quote";

        const ExprAst select = Parse("select(c, 'a', 'b')");
        EXPECT_EQ(select[select.root].argumentCount, 3U);
        EXPECT_EQ(PrintExpression(select), "select(c, 'a', 'b')");
        EXPECT_EQ(Printed("'x' != a"), "'x' != a");
    }

    TEST(ProgramParserTest, AStringMayBeEmptyAndHoldSpacesOperatorsAndDotsAsText) {
        const ExprAst empty = Parse("''");
        ASSERT_EQ(empty.nodes.size(), 1U);
        EXPECT_EQ(empty[empty.root].kind, ExprNodeKind::String);
        EXPECT_TRUE(empty[empty.root].text.empty());
        EXPECT_EQ(Printed("''"), "''");

        const ExprAst text = Parse("'a + b.c (d) && \"e\" [0] = 1  '");
        ASSERT_EQ(text.nodes.size(), 1U) << "what is inside the quotes is never parsed as code";
        EXPECT_EQ(text[text.root].text, "a + b.c (d) && \"e\" [0] = 1  ");
        EXPECT_EQ(Printed("'a + b'"), "'a + b'");
        EXPECT_EQ(Printed("'ab'.x"), "('ab').x");
    }

    TEST(ProgramParserTest, APrintedStringParsesToTheSameTree) {
        const StringView texts[] = {"a == 'Fire'", "select(a < b, 'x y', '')", "'a' == 'b' || c", "'it.s'.x", "f('a', 'b + c', 1)"};
        for (const StringView text : texts) {
            const ExprAst first = Parse(text);
            const String printed = PrintExpression(first);
            const ExprAst second = Parse(printed);
            EXPECT_TRUE(SameTree(first, second)) << String(text).c_str() << " -> " << printed.c_str();
            EXPECT_EQ(PrintExpression(second), printed);
        }
    }

    TEST(ProgramParserTest, ABadStringIsAnErrorAtTheRightByte) {
        ExpectError("a == 'Fire", 6, "unterminated string, expected ' to close it");
        ExpectError("'", 1, "unterminated string");
        ExpectError("a + 'b' + 'c", 11, "unterminated string");
        ExpectError("a == 'x\ny'", 8, "control character 0x0a");
        ExpectError("'a\tb'", 3, "control character 0x09");
        ExpectError("'a\x07'", 3, "control character 0x07");
        ExpectError("'a\x7f'", 3, "control character 0x7f");
        ExpectError(String("'a\0b'", 5), 3, "control character 0x00");
        const ProgramError invalid = ExpectError("'a\xFFz'", 3, "invalid UTF-8 byte 0xff");
        EXPECT_FALSE(Contains(invalid.message, "\xFF")) << "the bad byte is never copied into the message";
        ExpectError("'caf\xC3\xA9'", 5, "the character '\xC3\xA9'");
        ExpectError("'a' 'b'", 5, "expected an operator or the end of the formula, found the string 'b'");
        ExpectError("a 'b'", 3, "found the string 'b'");
    }

    TEST(ProgramParserTest, AStringCountsInTheLimits) {
        const String longest = "'" + Repeated("a", ExprLimits::kMaxTextBytes - 2) + "'";
        const ExprAst fits = Parse(longest);
        EXPECT_EQ(fits[fits.root].text.size(), ExprLimits::kMaxTextBytes - 2);
        ExpectError(longest + " ", ExprLimits::kMaxTextBytes + 1, "longer than 4096 bytes");
        ExpectError("'" + Repeated("a", ExprLimits::kMaxTextBytes) + "'", ExprLimits::kMaxTextBytes + 1, "longer than 4096 bytes");

        const auto list = [](size_t count) { return "f('a'" + Repeated(", 'a'", count - 1) + ")"; };
        EXPECT_EQ(Parse(list(ExprLimits::kMaxNodes - 1)).nodes.size(), ExprLimits::kMaxNodes);
        const auto parsed = ParseExpression(list(ExprLimits::kMaxNodes));
        ASSERT_FALSE(parsed.has_value());
        EXPECT_TRUE(Contains(parsed.error().message, "more than 512 parts")) << parsed.error().message.c_str();
    }

    TEST(ProgramParserTest, InvalidUtf8AndForeignCharactersAreErrorsNotCrashes) {
        const ProgramError invalid = ExpectError("1 + \xFF", 5, "invalid UTF-8 byte 0xff");
        EXPECT_FALSE(Contains(invalid.message, "\xFF")) << "the bad byte is never copied into the message";
        ExpectError("1 + \xE2\x82", 5, "invalid UTF-8 byte 0xe2");
        ExpectError("\xC0\x80", 1, "invalid UTF-8 byte 0xc0");
        ExpectError("\xED\xA0\x80", 1, "invalid UTF-8 byte 0xed");
        ExpectError("\x80", 1, "invalid UTF-8 byte 0x80");
        ExpectError("a + \xC3\xA9", 5, "the character '\xC3\xA9'");
        ExpectError("a + \xF0\x9F\x98\x80", 5, "the character");
        ExpectError("a +\x07 b", 4, "control character 0x07");
        ExpectError(String("a +\0 b", 6), 4, "control character 0x00");
    }

}  // namespace BECore::Tests
