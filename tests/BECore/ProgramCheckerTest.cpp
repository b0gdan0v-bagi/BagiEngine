// The type checker of formulas (Values/Programs/ExprChecker, ExprFunctions): the type rules of the language, the literals, the paths and the errors with
// their columns. The text is parsed with the real parser; the typed tree is read back node by node.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprFunctions.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <Common/Printers.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        bool Contains(const String& text, StringView part) {
            return text.find(part.data(), 0, part.size()) != String::npos;
        }

        /// The levels the tests read: `caster` and `cast` as in `ValueWorld`, and `behind`, whose members are not all fixed
        struct CheckerContext {
            ContextDecl declaration;

            CheckerContext() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(declaration.Add<ValueBehind>("behind").has_value());
            }
        };

        struct TypeRow {
            ProgramType type;
            StringView field;
        };

        const TypeRow kTypeRows[] = {{ProgramType::Bool, "caster.alive"},
                                     {ProgramType::Int32, "caster.attack"},
                                     {ProgramType::Int64, "caster.score"},
                                     {ProgramType::Float, "caster.speed"},
                                     {ProgramType::Double, "caster.precise"}};

        TypeDesc DescriptorOf(ProgramType type) {
            switch (type) {
                case ProgramType::Bool:
                    return DescribeType<bool>();
                case ProgramType::Int32:
                    return DescribeType<int32_t>();
                case ProgramType::Int64:
                    return DescribeType<int64_t>();
                case ProgramType::Float:
                    return DescribeType<float>();
                case ProgramType::Double:
                    return DescribeType<double>();
                default:
                    break;
            }
            return {};
        }

        std::expected<TypedExpr, ProgramError> Check(const ContextDecl& declaration, StringView text, ProgramType expected) {
            const auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed.has_value() ? String{} : parsed.error().message).c_str();
            if (!parsed.has_value()) {
                return std::unexpected(parsed.error());
            }
            return CheckExpression(*parsed, declaration, DescriptorOf(expected));
        }

        TypedExpr Accepted(const ContextDecl& declaration, StringView text, ProgramType expected) {
            auto checked = Check(declaration, text, expected);
            EXPECT_TRUE(checked.has_value()) << String(text).c_str() << ": " << (checked.has_value() ? String{} : checked.error().message).c_str();
            if (!checked.has_value()) {
                return {};
            }
            EXPECT_EQ(checked->type, expected) << String(text).c_str();
            EXPECT_EQ((*checked)[checked->root].type, expected) << String(text).c_str();
            return std::move(*checked);
        }

        /// The message must say every one of @p words and the column is where the text puts it
        ProgramError Refused(const ContextDecl& declaration, StringView text, ProgramType expected, uint32_t column, std::initializer_list<StringView> words) {
            const auto checked = Check(declaration, text, expected);
            EXPECT_FALSE(checked.has_value()) << String(text).c_str();
            if (checked.has_value()) {
                return {};
            }
            EXPECT_EQ(checked.error().column, column) << String(text).c_str() << ": " << checked.error().message.c_str();
            for (const StringView word : words) {
                EXPECT_TRUE(Contains(checked.error().message, word)) << String(text).c_str() << ": " << checked.error().message.c_str() << " does not say '" << String(word).c_str() << "'";
            }
            return checked.error();
        }

        /// The 1-based column of the first @p part of @p text
        uint32_t ColumnOf(StringView text, StringView part) {
            const size_t at = text.find(part);
            EXPECT_NE(at, StringView::npos) << String(part).c_str();
            return static_cast<uint32_t>(at + 1);
        }

        template <typename T>
        T ConstantOf(const TypedNode& node) {
            EXPECT_EQ(node.kind, TypedNodeKind::Constant);
            T value{};
            std::memcpy(&value, node.constant.bytes.data(), sizeof(T));
            return value;
        }

        const TypedNode& Root(const TypedExpr& expr) {
            return expr[expr.root];
        }

        bool IsOperator(ProgramOperation operation) {
            return operation != ProgramOperation::LoadSlot && operation != ProgramOperation::Neg && operation != ProgramOperation::Not && operation != ProgramOperation::Abs &&
                   operation != ProgramOperation::Min && operation != ProgramOperation::Max && operation != ProgramOperation::Clamp && operation != ProgramOperation::Select &&
                   operation != ProgramOperation::Lerp && operation != ProgramOperation::Floor && operation != ProgramOperation::Ceil && operation != ProgramOperation::Round &&
                   operation != ProgramOperation::Sqrt;
        }

        bool ResultIsBool(ProgramOperation operation) {
            return operation == ProgramOperation::Eq || operation == ProgramOperation::Ne || operation == ProgramOperation::Lt || operation == ProgramOperation::Le ||
                   operation == ProgramOperation::Gt || operation == ProgramOperation::Ge || operation == ProgramOperation::And || operation == ProgramOperation::Or;
        }

    }  // namespace

    TEST(ProgramCheckerTest, ATypedTreeIsInPostOrderWithTheSlotsAndConstantsOfTheFormula) {
        const CheckerContext context;
        const TypedExpr expr = Accepted(context.declaration, "caster.attack * (100 + cast.level * 10) / 100", ProgramType::Int32);
        // The operands that read the context are checked first, so `cast.level * 10` comes before the `100` beside it
        ASSERT_EQ(expr.nodes.size(), 9U);
        EXPECT_EQ(expr.root, 8U);
        EXPECT_EQ(Root(expr).opcode, Opcode::DivInt32);
        EXPECT_EQ(expr[0].kind, TypedNodeKind::Slot);
        EXPECT_EQ(expr[0].path, "caster.attack");
        EXPECT_EQ(expr[0].slot.level, 0);
        EXPECT_EQ(expr[1].kind, TypedNodeKind::Slot);
        EXPECT_EQ(expr[1].path, "cast.level");
        EXPECT_EQ(expr[1].slot.level, 1);
        EXPECT_EQ(ConstantOf<int32_t>(expr[2]), 10);
        EXPECT_EQ(expr[3].opcode, Opcode::MulInt32);
        EXPECT_EQ(ConstantOf<int32_t>(expr[4]), 100);
        EXPECT_EQ(expr[5].opcode, Opcode::AddInt32);
        EXPECT_EQ(expr[6].opcode, Opcode::MulInt32);
        EXPECT_EQ(ConstantOf<int32_t>(expr[7]), 100);
        for (TypedNodeId id = 0; id < expr.nodes.size(); ++id) {
            for (uint8_t i = 0; i < expr[id].operandCount; ++i) {
                EXPECT_LT(expr[id].operands[i], id) << "an operand is an earlier node";
            }
        }
    }

    TEST(ProgramCheckerTest, ASlotIsLinkedLikeLinkSlotLinksIt) {
        const CheckerContext context;
        const TypedExpr expr = Accepted(context.declaration, "caster.span.low", ProgramType::Int32);
        ASSERT_EQ(expr.nodes.size(), 1U);
        const auto slot = LinkSlot(context.declaration, "caster.span.low", DescribeType<int32_t>());
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(Root(expr).kind, TypedNodeKind::Slot);
        EXPECT_EQ(Root(expr).slot.level, slot->level);
        EXPECT_EQ(Root(expr).slot.offset, slot->offset);
        EXPECT_EQ(Root(expr).path, "caster.span.low");
        Accepted(context.declaration, "cast.power", ProgramType::Float);
        Accepted(context.declaration, "caster.alive", ProgramType::Bool);
        Accepted(context.declaration, "caster.score", ProgramType::Int64);
        Accepted(context.declaration, "behind.span.high", ProgramType::Int32);
    }

    // Literals

    TEST(ProgramCheckerTest, AnIntegerLiteralTakesTheTypeOfTheOperandBesideIt) {
        const CheckerContext context;
        for (const TypeRow& row : kTypeRows) {
            if (row.type == ProgramType::Bool) {
                continue;
            }
            const String text = Format("{} + 12", row.field);
            const TypedExpr expr = Accepted(context.declaration, text, row.type);
            ASSERT_EQ(expr.nodes.size(), 3U) << text.c_str();
            EXPECT_EQ(expr[1].type, row.type) << text.c_str();
            switch (row.type) {
                case ProgramType::Int32:
                    EXPECT_EQ(ConstantOf<int32_t>(expr[1]), 12);
                    break;
                case ProgramType::Int64:
                    EXPECT_EQ(ConstantOf<int64_t>(expr[1]), 12);
                    break;
                case ProgramType::Float:
                    EXPECT_EQ(ConstantOf<float>(expr[1]), 12.0f);
                    break;
                default:
                    EXPECT_EQ(ConstantOf<double>(expr[1]), 12.0);
                    break;
            }
        }
        const TypedExpr left = Accepted(context.declaration, "12 + caster.speed", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(left[1]), 12.0f) << "the side the literal is on does not matter";
    }

    TEST(ProgramCheckerTest, AFloatLiteralIsFloatOrDoubleAndRefusedBesideAnInteger) {
        const CheckerContext context;
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "caster.speed + 1.5", ProgramType::Float)[1]), 1.5f);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "caster.precise + 1.5", ProgramType::Double)[1]), 1.5);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "caster.precise * 0.1", ProgramType::Double)[1]), 0.1) << "a double literal keeps its own precision";
        Refused(context.declaration, "caster.attack + 1.5", ProgramType::Int32, ColumnOf("caster.attack + 1.5", "1.5"), {"1.5", "float literal", "an int"});
        Refused(context.declaration, "caster.score * .5", ProgramType::Int64, ColumnOf("caster.score * .5", ".5"), {"float literal", "an int64"});
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "1e3", ProgramType::Float)[0]), 1000.0f);
        Refused(context.declaration, "1e39", ProgramType::Float, 1, {"out of the range of a float"});
        Refused(context.declaration, "1e999", ProgramType::Double, 1, {"out of the range of a double"});
    }

    TEST(ProgramCheckerTest, ALiteralAloneTakesTheExpectedType) {
        const CheckerContext context;
        EXPECT_EQ(ConstantOf<int32_t>(Accepted(context.declaration, "5", ProgramType::Int32)[0]), 5);
        EXPECT_EQ(ConstantOf<int64_t>(Accepted(context.declaration, "5", ProgramType::Int64)[0]), 5);
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "5", ProgramType::Float)[0]), 5.0f);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "5", ProgramType::Double)[0]), 5.0);
        Refused(context.declaration, "5", ProgramType::Bool, 1, {"the formula is an int", "a value of bool is asked for"});

        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "1.5", ProgramType::Float)[0]), 1.5f);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "1.5", ProgramType::Double)[0]), 1.5);
        Refused(context.declaration, "1.5", ProgramType::Int32, 1, {"float literal", "an int"});
        Refused(context.declaration, "1.5", ProgramType::Int64, 1, {"float literal", "an int64"});
        Refused(context.declaration, "1.5", ProgramType::Bool, 1, {"the formula is a float"});

        EXPECT_EQ(ConstantOf<int64_t>(Accepted(context.declaration, "3000000000", ProgramType::Int64)[0]), 3000000000);
        Refused(context.declaration, "3000000000", ProgramType::Int32, 1, {"3000000000", "does not fit an int"});
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "3000000000", ProgramType::Double)[0]), 3000000000.0);
    }

    TEST(ProgramCheckerTest, AnIntegerLiteralWithoutAnyTypeIsInt32UnlessItDoesNotFit) {
        const CheckerContext context;
        // `1 < 2` has no operand that says a type and no expected number: the literals are what they spell
        const TypedExpr small = Accepted(context.declaration, "2147483647 < 2147483647", ProgramType::Bool);
        EXPECT_EQ(small[0].type, ProgramType::Int32);
        const TypedExpr large = Accepted(context.declaration, "2147483648 < 3", ProgramType::Bool);
        EXPECT_EQ(large[0].type, ProgramType::Int64);
        EXPECT_EQ(large[1].type, ProgramType::Int64) << "the other side follows";
        const TypedExpr floats = Accepted(context.declaration, "1 < 2.5", ProgramType::Bool);
        EXPECT_EQ(floats[0].type, ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(floats[0]), 1.0f);
        EXPECT_EQ(ConstantOf<float>(floats[1]), 2.5f);
    }

    TEST(ProgramCheckerTest, TheRangesOfIntegerLiteralsAreChecked) {
        const CheckerContext context;
        EXPECT_EQ(ConstantOf<int32_t>(Accepted(context.declaration, "-2147483648", ProgramType::Int32)[0]), std::numeric_limits<int32_t>::min());
        EXPECT_EQ(ConstantOf<int32_t>(Accepted(context.declaration, "2147483647", ProgramType::Int32)[0]), std::numeric_limits<int32_t>::max());
        Refused(context.declaration, "2147483648", ProgramType::Int32, 1, {"does not fit an int"});
        Refused(context.declaration, "-2147483649", ProgramType::Int32, 1, {"does not fit an int"});
        EXPECT_EQ(ConstantOf<int64_t>(Accepted(context.declaration, "-9223372036854775808", ProgramType::Int64)[0]), std::numeric_limits<int64_t>::min());
        EXPECT_EQ(ConstantOf<int64_t>(Accepted(context.declaration, "9223372036854775807", ProgramType::Int64)[0]), std::numeric_limits<int64_t>::max());
        Refused(context.declaration, "9223372036854775808", ProgramType::Int64, 1, {"does not fit an int64"});
        Refused(context.declaration, "99999999999999999999", ProgramType::Int64, 1, {"does not fit an int64"});
    }

    TEST(ProgramCheckerTest, ASignedLiteralIsOneConstant) {
        const CheckerContext context;
        const TypedExpr negative = Accepted(context.declaration, "-5", ProgramType::Int32);
        ASSERT_EQ(negative.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<int32_t>(negative[0]), -5);
        EXPECT_EQ(ConstantOf<int32_t>(Accepted(context.declaration, "- -5", ProgramType::Int32)[0]), 5);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "-.25", ProgramType::Double)[0]), -0.25);
        const TypedExpr beside = Accepted(context.declaration, "caster.speed * -2", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(beside[1]), -2.0f);
    }

    TEST(ProgramCheckerTest, AConstantSubtreeFollowsTheOperandThatReadsTheContext) {
        const CheckerContext context;
        const TypedExpr expr = Accepted(context.declaration, "(2 * 3) * caster.speed", ProgramType::Float);
        EXPECT_EQ(expr[0].kind, TypedNodeKind::Slot);
        EXPECT_EQ(ConstantOf<float>(expr[1]), 2.0f);
        EXPECT_EQ(ConstantOf<float>(expr[2]), 3.0f);
        const TypedExpr compared = Accepted(context.declaration, "(1 + 2) < caster.precise", ProgramType::Bool);
        EXPECT_EQ(compared[1].type, ProgramType::Double);
        const TypedExpr whole = Accepted(context.declaration, "2 * 6", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(whole[0]), 2.0f) << "alone, a constant subtree takes the expected type";
        Refused(context.declaration, "1.5 * caster.attack", ProgramType::Int32, 1, {"1.5", "float literal"});
    }

    // Operators and functions

    TEST(ProgramCheckerTest, EveryBinaryOperatorIsAcceptedForTheTypesThatHaveItAndRefusedForTheOthers) {
        const CheckerContext context;
        for (const ProgramOperation operation :
             {ProgramOperation::Add, ProgramOperation::Sub, ProgramOperation::Mul, ProgramOperation::Div, ProgramOperation::Mod, ProgramOperation::Eq, ProgramOperation::Ne, ProgramOperation::Lt,
              ProgramOperation::Le, ProgramOperation::Gt, ProgramOperation::Ge, ProgramOperation::And, ProgramOperation::Or}) {
            ASSERT_TRUE(IsOperator(operation));
            for (const TypeRow& row : kTypeRows) {
                const String text = Format("{} {} {}", row.field, OperatorSymbol(operation), row.field);
                const ProgramType expected = ResultIsBool(operation) ? ProgramType::Bool : row.type;
                const Opcode opcode = OpcodeOf(operation, row.type);
                if (opcode != Opcode::Invalid) {
                    const TypedExpr expr = Accepted(context.declaration, text, expected);
                    EXPECT_EQ(Root(expr).opcode, opcode) << text.c_str();
                    EXPECT_EQ(Root(expr).operandCount, 2) << text.c_str();
                } else {
                    const bool logic = operation == ProgramOperation::And || operation == ProgramOperation::Or;
                    Refused(context.declaration, text, expected, logic ? 1 : ColumnOf(text, OperatorSymbol(operation)),
                            {logic ? "needs bool operands" : "is not defined for", ProgramTypeName(row.type)});
                }
            }
        }
    }

    TEST(ProgramCheckerTest, ARefusedOperatorListsTheTypesThatHaveIt) {
        const CheckerContext context;
        Refused(context.declaration, "caster.speed % 2", ProgramType::Float, 14, {"'%'", "not defined for float", "int or int64"});
        Refused(context.declaration, "caster.alive + caster.alive", ProgramType::Bool, 14, {"'+'", "not defined for bool", "int, int64, float or double"});
        Refused(context.declaration, "caster.alive < caster.alive", ProgramType::Bool, 14, {"'<'", "not defined for bool"});
        Refused(context.declaration, "-caster.alive", ProgramType::Bool, 1, {"'-'", "not defined for bool"});
    }

    TEST(ProgramCheckerTest, UnaryMinusAndNotTakeTheirOperandTypes) {
        const CheckerContext context;
        for (const TypeRow& row : kTypeRows) {
            const String negated = Format("-{}", row.field);
            if (row.type != ProgramType::Bool) {
                EXPECT_EQ(Root(Accepted(context.declaration, negated, row.type)).opcode, OpcodeOf(ProgramOperation::Neg, row.type));
            } else {
                Refused(context.declaration, negated, row.type, 1, {"bool"});
            }
            const String inverted = Format("!{}", row.field);
            if (row.type == ProgramType::Bool) {
                EXPECT_EQ(Root(Accepted(context.declaration, inverted, ProgramType::Bool)).opcode, Opcode::NotBool);
            } else {
                Refused(context.declaration, inverted, ProgramType::Bool, ColumnOf(inverted, row.field), {"'!' needs a bool", ProgramTypeName(row.type)});
            }
        }
    }

    TEST(ProgramCheckerTest, EveryFunctionIsAcceptedForTheTypesThatHaveItAndRefusedForTheOthers) {
        const CheckerContext context;
        for (const ExprFunction& function : kExprFunctions) {
            if (function.shape != ExprFunctionShape::SameType) {
                continue;
            }
            for (const TypeRow& row : kTypeRows) {
                String text = Format("{}(", function.name);
                for (uint8_t i = 0; i < function.arity; ++i) {
                    text += Format("{}{}", i == 0 ? "" : ", ", row.field);
                }
                text += ")";
                const Opcode opcode = OpcodeOf(function.operation, row.type);
                if (opcode != Opcode::Invalid) {
                    const TypedExpr expr = Accepted(context.declaration, text, row.type);
                    EXPECT_EQ(Root(expr).opcode, opcode) << text.c_str();
                    EXPECT_EQ(Root(expr).operandCount, function.arity) << text.c_str();
                } else {
                    Refused(context.declaration, text, row.type, 1, {function.name, "is not defined for", ProgramTypeName(row.type)});
                }
            }
        }
    }

    TEST(ProgramCheckerTest, SelectTakesABoolAndTwoValuesOfOneType) {
        const CheckerContext context;
        for (const TypeRow& row : kTypeRows) {
            const String text = Format("select(caster.alive, {}, {})", row.field, row.field);
            const TypedExpr expr = Accepted(context.declaration, text, row.type);
            EXPECT_EQ(Root(expr).opcode, OpcodeOf(ProgramOperation::Select, row.type));
            ASSERT_EQ(Root(expr).operandCount, 3);
            EXPECT_EQ(expr[Root(expr).operands[0]].type, ProgramType::Bool) << "the condition comes first";
        }
        const TypedExpr literals = Accepted(context.declaration, "select(caster.alive, 1, 2.5)", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(literals[1]), 1.0f);
        const String mismatch = "select(caster.alive, caster.attack, caster.speed)";
        Refused(context.declaration, mismatch, ProgramType::Int32, ColumnOf(mismatch, "caster.speed"), {"'select'", "one type", "int and float", "float(x)"});
        Refused(context.declaration, "select(caster.attack, 1, 2)", ProgramType::Int32, 8, {"bool as its first argument", "an int"});
        Refused(context.declaration, "select(caster.alive, caster.alive, 1)", ProgramType::Bool, 36, {"bool and int"});
    }

    TEST(ProgramCheckerTest, AConversionIsAFunctionAndTheSameTypeIsNoOperation) {
        const CheckerContext context;
        struct Row {
            StringView name;
            ProgramType target;
        };
        for (const Row& target : {Row{"int", ProgramType::Int32}, Row{"int64", ProgramType::Int64}, Row{"float", ProgramType::Float}, Row{"double", ProgramType::Double}}) {
            for (const TypeRow& source : kTypeRows) {
                const String text = Format("{}({})", target.name, source.field);
                if (source.type == ProgramType::Bool) {
                    Refused(context.declaration, text, target.target, ColumnOf(text, source.field), {"converts a number", "bool"});
                } else if (source.type == target.target) {
                    const TypedExpr expr = Accepted(context.declaration, text, target.target);
                    EXPECT_EQ(expr.nodes.size(), 1U) << text.c_str() << " changes nothing";
                    EXPECT_EQ(Root(expr).kind, TypedNodeKind::Slot);
                } else {
                    const TypedExpr expr = Accepted(context.declaration, text, target.target);
                    EXPECT_EQ(Root(expr).opcode, ConversionOpcode(source.type, target.target)) << text.c_str();
                    EXPECT_EQ(Root(expr).operandCount, 1);
                }
            }
        }
    }

    TEST(ProgramCheckerTest, TheLiteralInAConversionIsReadAsWhatItSpells) {
        const CheckerContext context;
        const TypedExpr exact = Accepted(context.declaration, "float(0.1)", ProgramType::Float);
        EXPECT_EQ(Root(exact).opcode, Opcode::DoubleToFloat);
        EXPECT_EQ(ConstantOf<double>(exact[Root(exact).operands[0]]), 0.1) << "the literal is a double, rounded once by the conversion";
        const TypedExpr same = Accepted(context.declaration, "double(0.1)", ProgramType::Double);
        EXPECT_EQ(same.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<double>(same[0]), 0.1);
        const TypedExpr truncated = Accepted(context.declaration, "int(2.7)", ProgramType::Int32);
        EXPECT_EQ(Root(truncated).opcode, Opcode::DoubleToInt32);
        const TypedExpr widened = Accepted(context.declaration, "float(5)", ProgramType::Float);
        EXPECT_EQ(widened.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<float>(widened[0]), 5.0f);
        Accepted(context.declaration, "caster.attack * int(caster.speed)", ProgramType::Int32);
    }

    TEST(ProgramCheckerTest, TwoTypesAreAnErrorThatNamesBothAndSaysHowToConvert) {
        const CheckerContext context;
        const String text = "caster.attack * caster.speed";
        const ProgramError error = Refused(context.declaration, text, ProgramType::Float, ColumnOf(text, "caster.speed"), {"'*'", "int and float", "no implicit conversion", "float(x)", "int(x)"});
        EXPECT_FALSE(error.message.empty());
        Refused(context.declaration, "caster.score + caster.attack", ProgramType::Int64, ColumnOf("caster.score + caster.attack", "caster.attack"), {"int64 and int"});
        Refused(context.declaration, "caster.alive == caster.attack", ProgramType::Bool, ColumnOf("caster.alive == caster.attack", "caster.attack"), {"bool and int"});
        Refused(context.declaration, "caster.alive == 1", ProgramType::Bool, 17, {"bool and int"});
        Refused(context.declaration, "min(caster.attack, caster.score)", ProgramType::Int32, 20, {"'min'", "int and int64"});
        Refused(context.declaration, "clamp(caster.speed, 0, caster.precise)", ProgramType::Float, 24, {"'clamp'", "float and double"});
    }

    TEST(ProgramCheckerTest, TheResultMustBeExactlyTheExpectedType) {
        const CheckerContext context;
        Refused(context.declaration, "caster.attack", ProgramType::Float, 1, {"the formula is an int", "value of float is asked for", "float(x)"});
        Refused(context.declaration, "caster.speed * 2", ProgramType::Double, 14, {"the formula is a float", "double"});
        Refused(context.declaration, "caster.attack > 3", ProgramType::Int32, 15, {"the formula is a bool", "value of int"});
        Refused(context.declaration, "caster.attack", ProgramType::Bool, 1, {"the formula is an int", "bool"});
    }

    TEST(ProgramCheckerTest, AnExpectedTypeOutsideTheProgramTypesListsThem) {
        const CheckerContext context;
        const auto parsed = ParseExpression("1");
        ASSERT_TRUE(parsed.has_value());
        const auto vector = CheckExpression(*parsed, context.declaration, DescribeType<Vec3>());
        ASSERT_FALSE(vector.has_value());
        EXPECT_EQ(vector.error().column, 0U);
        EXPECT_TRUE(Contains(vector.error().message, "bool, int, int64, float or double")) << vector.error().message.c_str();
        EXPECT_TRUE(Contains(vector.error().message, "Vec3")) << vector.error().message.c_str();
        const auto unsignedType = CheckExpression(*parsed, context.declaration, DescribeType<uint32_t>());
        ASSERT_FALSE(unsignedType.has_value());
        EXPECT_TRUE(Contains(unsignedType.error().message, "uint32")) << unsignedType.error().message.c_str();
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<String>()).has_value());
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<ValueElement>()).has_value());
    }

    TEST(ProgramCheckerTest, LogicNeedsBoolsOnBothSides) {
        const CheckerContext context;
        Accepted(context.declaration, "caster.alive && cast.level > 2", ProgramType::Bool);
        Accepted(context.declaration, "caster.alive || !caster.alive", ProgramType::Bool);
        Accepted(context.declaration, "true && caster.alive == false", ProgramType::Bool);
        Refused(context.declaration, "caster.alive && caster.attack", ProgramType::Bool, 17, {"'&&'", "right one is an int", "!= 0"});
        Refused(context.declaration, "caster.attack || caster.alive", ProgramType::Bool, 1, {"'||'", "left one is an int"});
        Refused(context.declaration, "caster.alive && 1", ProgramType::Bool, 17, {"right one is an int"});
        Refused(context.declaration, "caster.speed && 1.5", ProgramType::Bool, 1, {"left one is a float"});
    }

    TEST(ProgramCheckerTest, ADivisionOrModuloByALiteralZeroIsAnError) {
        const CheckerContext context;
        Refused(context.declaration, "caster.attack / 0", ProgramType::Int32, 17, {"division by a literal zero"});
        Refused(context.declaration, "caster.attack % 0", ProgramType::Int32, 17, {"modulo by a literal zero"});
        Refused(context.declaration, "caster.speed / 0.0", ProgramType::Float, 16, {"division by a literal zero"});
        Refused(context.declaration, "caster.speed / -0", ProgramType::Float, 16, {"division by a literal zero"});
        Refused(context.declaration, "1 / 0", ProgramType::Int32, 5, {"division"});
        Accepted(context.declaration, "caster.attack / 1", ProgramType::Int32);
        Accepted(context.declaration, "caster.attack / (3 - 3)", ProgramType::Int32);
        Accepted(context.declaration, "caster.attack / cast.level", ProgramType::Int32);
    }

    TEST(ProgramCheckerTest, ACallNeedsAKnownFunctionAndItsArguments) {
        const CheckerContext context;
        Refused(context.declaration, "min(caster.attack)", ProgramType::Int32, 1, {"'min' takes 2 arguments", "1 is given"});
        Refused(context.declaration, "abs(1, 2)", ProgramType::Int32, 1, {"'abs' takes 1 argument,", "2 are given"});
        Refused(context.declaration, "select(1)", ProgramType::Int32, 1, {"'select' takes 3"});
        Refused(context.declaration, "round()", ProgramType::Float, 1, {"takes 1 argument", "0 are given"});
        const ProgramError unknown = Refused(context.declaration, "frobnicate(1)", ProgramType::Int32, 1, {"unknown function 'frobnicate'", "min", "clamp", "double"});
        EXPECT_FALSE(unknown.message.empty());
        Refused(context.declaration, "caster(1)", ProgramType::Int32, 1, {"'caster' is a level, not a function"});
        Refused(context.declaration, "caster.attack(1)", ProgramType::Int32, 1, {"unknown function 'caster.attack'"});
    }

    TEST(ProgramCheckerTest, ANameThatIsNeitherALevelNorAFunctionIsUnknownAndListsWhatIsDeclared) {
        const CheckerContext context;
        Refused(context.declaration, "frob", ProgramType::Int32, 1, {"unknown name 'frob'", "declared levels are: caster, cast, behind", "functions are"});
        Refused(context.declaration, "1 + castr", ProgramType::Int32, 5, {"unknown name 'castr'", "did you mean 'caster'"});
        Refused(context.declaration, "caster", ProgramType::Int32, 1, {"'caster' is a level", "field"});
        Refused(context.declaration, "min", ProgramType::Int32, 1, {"'min' is a function", "min(...)"});
        const ContextDecl empty;
        Refused(empty, "caster.attack", ProgramType::Int32, 1, {"unknown level 'caster'"});
        Refused(empty, "x", ProgramType::Int32, 1, {"declared levels are: none"});
    }

    TEST(ProgramCheckerTest, APathThatLinkSlotRefusesKeepsItsMessageAndThePositionOfTheName) {
        const CheckerContext context;
        const String text = "1 + caster.attak";
        const ProgramError missing =
            Refused(context.declaration, text, ProgramType::Int32, ColumnOf(text, "caster.attak"), {"caster.attak", "has no field 'attak'", "did you mean 'attack'", "fields are"});
        EXPECT_FALSE(missing.message.empty());
        Refused(context.declaration, "hero.attack", ProgramType::Int32, 1, {"unknown level 'hero'", "caster", "cast"});
        Refused(context.declaration, "casster.attack", ProgramType::Int32, 1, {"did you mean 'caster'"});
        Refused(context.declaration, "caster.span.nope", ProgramType::Int32, 1, {"caster.span.nope", "nope"});
        Refused(context.declaration, "behind.maybe", ProgramType::Int32, 1, {"behind.maybe", "optional"});
    }

    TEST(ProgramCheckerTest, AFieldOfAnotherTypeIsNamedWithItsTypeAndWhatAFormulaReads) {
        const CheckerContext context;
        Refused(context.declaration, "caster.count", ProgramType::Int32, 1, {"caster.count", "uint32", "bool, int, int64, float or double"});
        Refused(context.declaration, "caster.velocity.x + 1", ProgramType::Float, 1, {"caster.velocity.x"});
        Refused(context.declaration, "caster.velocity", ProgramType::Float, 1, {"caster.velocity", "Vec3", "reads only fields of the types"});
        Refused(context.declaration, "caster.span", ProgramType::Int32, 1, {"ValueSpan"});
        Refused(context.declaration, "caster.element", ProgramType::Int32, 1, {"enum"});
        Refused(context.declaration, "caster.title", ProgramType::Int32, 1, {"caster.title"});
        Refused(context.declaration, "caster.buffs", ProgramType::Int32, 1, {"list"});
        Refused(context.declaration, "behind.table", ProgramType::Int32, 1, {"map"});
        Refused(context.declaration, "caster.cell", ProgramType::Int32, 1, {"IVec2"});
    }

    TEST(ProgramCheckerTest, ListElementsAndMembersNeverReachTheChecker) {
        EXPECT_FALSE(ParseExpression("caster.buffs[0]").has_value()) << "an index is refused by the lexer";
        const CheckerContext context;
        Refused(context.declaration, "(caster.attack).x", ProgramType::Int32, 16, {"'.x'", "no type has components"});
        Refused(context.declaration, "min(caster.attack, 1).y + 1", ProgramType::Int32, 22, {"'.y'"});
    }

    TEST(ProgramCheckerTest, AnErrorColumnPointsAtTheNodeThatIsWrong) {
        const CheckerContext context;
        const String sum = "caster.attack + (cast.level * caster.speed)";
        Refused(context.declaration, sum, ProgramType::Int32, ColumnOf(sum, "caster.speed"), {"int and float"});
        const String nested = "min(1, max(caster.speed, caster.nope))";
        Refused(context.declaration, nested, ProgramType::Float, ColumnOf(nested, "caster.nope"), {"nope"});
        const String logic = "caster.alive && (cast.level > 2 || caster.attack)";
        Refused(context.declaration, logic, ProgramType::Bool, ColumnOf(logic, "caster.attack"), {"right one is an int"});
        const String zero = "caster.attack + caster.attack / 0";
        Refused(context.declaration, zero, ProgramType::Int32, ColumnOf(zero, "0"), {"division"});
    }

}  // namespace BECore::Tests
