// The type checker of formulas (Values/Programs/ExprChecker, ExprFunctions): the type rules of the language, the literals, the paths and the errors with
// their columns. The text is parsed with the real parser; the typed tree is read back node by node.

#include <BECore/ProgramDomainData.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/CompileOptions.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprFunctions.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/ValueTables.h>
#include <Common/Printers.h>
#include <Common/ScratchScopeFixture.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using ProgramCheckerTest = ScratchScopeFixture;

    using namespace ValueTestData;

    namespace {

        /// The levels the tests read: `caster` and `cast` as in `ValueWorld`, and `behind`, whose members are not all fixed
        struct CheckerContext {
            ContextDecl declaration;

            CheckerContext() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(declaration.Add<ValueBehind>("behind").has_value());
                EXPECT_TRUE(declaration.Add<ValueEnumWidths>("widths").has_value());
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

        /// The descriptor of a program type that is not an enum (a field of the checker context has one of each)
        const TypeDesc& DescriptorOf(ProgramType type) {
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
                case ProgramType::Vec2:
                    return DescribeType<Vec2>();
                case ProgramType::Vec3:
                    return DescribeType<Vec3>();
                case ProgramType::Vec4:
                    return DescribeType<Vec4>();
                case ProgramType::IVec2:
                    return DescribeType<IVec2>();
                case ProgramType::Quat:
                    return DescribeType<Quat>();
                case ProgramType::Color:
                    return DescribeType<Color>();
                case ProgramType::String:
                    return DescribeType<PoolString>();
                case ProgramType::Domain:
                    return DescribeType<ProgramDomainData::DomainBits256>();
                case ProgramType::Enum:
                    break;
            }
            return DescribeType<ValueElement>();
        }

        std::expected<TypedExpr, ProgramError> Check(const ContextDecl& declaration, StringView text, const TypeDesc& expected) {
            const auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str() << ": " << (parsed.has_value() ? String{} : parsed.error().message).c_str();
            if (!parsed.has_value()) {
                return std::unexpected(parsed.error());
            }
            return CheckExpression(*parsed, declaration, expected);
        }

        std::expected<TypedExpr, ProgramError> Check(const ContextDecl& declaration, StringView text, ProgramType expected) {
            return Check(declaration, text, DescriptorOf(expected));
        }

        /// The formula must check and have the type the field of @p expected has (for an enum, the very enum)
        TypedExpr AcceptedAs(const ContextDecl& declaration, StringView text, const TypeDesc& expected) {
            auto checked = Check(declaration, text, expected);
            EXPECT_TRUE(checked.has_value()) << String(text).c_str() << ": " << (checked.has_value() ? String{} : checked.error().message).c_str();
            if (!checked.has_value()) {
                return {};
            }
            const auto wanted = CheckExpectedType(expected);
            EXPECT_TRUE(wanted.has_value());
            if (wanted.has_value()) {
                EXPECT_TRUE(checked->type == *wanted) << String(text).c_str();
                EXPECT_TRUE((*checked)[checked->root].type == *wanted) << String(text).c_str();
            }
            return std::move(*checked);
        }

        TypedExpr Accepted(const ContextDecl& declaration, StringView text, ProgramType expected) {
            return AcceptedAs(declaration, text, DescriptorOf(expected));
        }

        /// The message must say every one of @p words and the column is where the text puts it
        ProgramError RefusedAs(const ContextDecl& declaration, StringView text, const TypeDesc& expected, uint32_t column, std::initializer_list<StringView> words) {
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

        ProgramError Refused(const ContextDecl& declaration, StringView text, ProgramType expected, uint32_t column, std::initializer_list<StringView> words) {
            return RefusedAs(declaration, text, DescriptorOf(expected), column, words);
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

    TEST_F(ProgramCheckerTest, ATypedTreeIsInPostOrderWithTheSlotsAndConstantsOfTheFormula) {
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

    TEST_F(ProgramCheckerTest, ASlotIsLinkedLikeLinkSlotLinksIt) {
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

    TEST_F(ProgramCheckerTest, AnIntegerLiteralTakesTheTypeOfTheOperandBesideIt) {
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

    TEST_F(ProgramCheckerTest, AFloatLiteralIsFloatOrDoubleAndRefusedBesideAnInteger) {
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

    TEST_F(ProgramCheckerTest, ALiteralAloneTakesTheExpectedType) {
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

    TEST_F(ProgramCheckerTest, AnIntegerLiteralWithoutAnyTypeIsInt32UnlessItDoesNotFit) {
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

    TEST_F(ProgramCheckerTest, TheRangesOfIntegerLiteralsAreChecked) {
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

    TEST_F(ProgramCheckerTest, ASignedLiteralIsOneConstant) {
        const CheckerContext context;
        const TypedExpr negative = Accepted(context.declaration, "-5", ProgramType::Int32);
        ASSERT_EQ(negative.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<int32_t>(negative[0]), -5);
        EXPECT_EQ(ConstantOf<int32_t>(Accepted(context.declaration, "- -5", ProgramType::Int32)[0]), 5);
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "-.25", ProgramType::Double)[0]), -0.25);
        const TypedExpr beside = Accepted(context.declaration, "caster.speed * -2", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(beside[1]), -2.0f);
    }

    TEST_F(ProgramCheckerTest, AConstantSubtreeFollowsTheOperandThatReadsTheContext) {
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

    TEST_F(ProgramCheckerTest, EveryBinaryOperatorIsAcceptedForTheTypesThatHaveItAndRefusedForTheOthers) {
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

    TEST_F(ProgramCheckerTest, ARefusedOperatorListsTheTypesThatHaveIt) {
        const CheckerContext context;
        Refused(context.declaration, "caster.speed % 2", ProgramType::Float, 14, {"'%'", "not defined for float", "int or int64"});
        Refused(context.declaration, "caster.alive + caster.alive", ProgramType::Bool, 14, {"'+'", "not defined for bool", "int, int64, float, double, vec2, vec3, vec4 or ivec2"});
        Refused(context.declaration, "caster.alive < caster.alive", ProgramType::Bool, 14, {"'<'", "not defined for bool"});
        Refused(context.declaration, "-caster.alive", ProgramType::Bool, 1, {"'-'", "not defined for bool"});
    }

    TEST_F(ProgramCheckerTest, UnaryMinusAndNotTakeTheirOperandTypes) {
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

    TEST_F(ProgramCheckerTest, EveryFunctionIsAcceptedForTheTypesThatHaveItAndRefusedForTheOthers) {
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

    TEST_F(ProgramCheckerTest, SelectTakesABoolAndTwoValuesOfOneType) {
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

    TEST_F(ProgramCheckerTest, AConversionIsAFunctionAndTheSameTypeIsNoOperation) {
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

    TEST_F(ProgramCheckerTest, TheLiteralInAConversionIsReadAsWhatItSpells) {
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

    TEST_F(ProgramCheckerTest, TwoTypesAreAnErrorThatNamesBothAndSaysHowToConvert) {
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

    TEST_F(ProgramCheckerTest, TheResultMustBeExactlyTheExpectedType) {
        const CheckerContext context;
        Refused(context.declaration, "caster.attack", ProgramType::Float, 1, {"the formula is an int", "value of float is asked for", "float(x)"});
        Refused(context.declaration, "caster.speed * 2", ProgramType::Double, 14, {"the formula is a float", "double"});
        Refused(context.declaration, "caster.attack > 3", ProgramType::Int32, 15, {"the formula is a bool", "value of int"});
        Refused(context.declaration, "caster.attack", ProgramType::Bool, 1, {"the formula is an int", "bool"});
    }

    TEST_F(ProgramCheckerTest, AnExpectedTypeOutsideTheProgramTypesListsThem) {
        const CheckerContext context;
        const auto parsed = ParseExpression("1");
        ASSERT_TRUE(parsed.has_value());
        const auto rect = CheckExpression(*parsed, context.declaration, DescribeType<Rect>());
        ASSERT_FALSE(rect.has_value());
        EXPECT_EQ(rect.error().column, 0U);
        EXPECT_TRUE(Contains(rect.error().message, "bool, int, int64, float, double, vec2, vec3, vec4, ivec2, quat, color, enum")) << rect.error().message.c_str();
        EXPECT_TRUE(Contains(rect.error().message, "Rect")) << rect.error().message.c_str();
        const auto unsignedType = CheckExpression(*parsed, context.declaration, DescribeType<uint32_t>());
        ASSERT_FALSE(unsignedType.has_value());
        EXPECT_TRUE(Contains(unsignedType.error().message, "uint32")) << unsignedType.error().message.c_str();
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<String>()).has_value());
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<ValuePoint>()).has_value());
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<Vector<int32_t>>()).has_value());
    }

    TEST_F(ProgramCheckerTest, EveryProgramTypeAndEveryEnumIsAnExpectedType) {
        const TypeDesc* const descriptors[] = {&DescribeType<bool>(),  &DescribeType<int32_t>(),    &DescribeType<int64_t>(),      &DescribeType<float>(),        &DescribeType<double>(),
                                               &DescribeType<Vec2>(),  &DescribeType<Vec3>(),       &DescribeType<Vec4>(),         &DescribeType<IVec2>(),        &DescribeType<Quat>(),
                                               &DescribeType<Color>(), &DescribeType<PoolString>(), &DescribeType<ValueElement>(), &DescribeType<ValueWideEnum>()};
        for (const TypeDesc* descriptor : descriptors) {
            const auto wanted = CheckExpectedType(*descriptor);
            ASSERT_TRUE(wanted.has_value()) << wanted.error().message.c_str();
            EXPECT_EQ(wanted->program == ProgramType::Enum, descriptor->kind == TypeKind::Enum);
        }
        EXPECT_FALSE(*CheckExpectedType(DescribeType<ValueElement>()) == *CheckExpectedType(DescribeType<ValueWideEnum>())) << "two enums are two types";
        EXPECT_TRUE(*CheckExpectedType(DescribeType<ValueElement>()) == *CheckExpectedType(DescribeType<ValueElement>()));
        EXPECT_FALSE(CheckExpectedType(DescribeType<String>()).has_value()) << "a String is not a PoolString";
    }

    TEST_F(ProgramCheckerTest, LogicNeedsBoolsOnBothSides) {
        const CheckerContext context;
        Accepted(context.declaration, "caster.alive && cast.level > 2", ProgramType::Bool);
        Accepted(context.declaration, "caster.alive || !caster.alive", ProgramType::Bool);
        Accepted(context.declaration, "true && caster.alive == false", ProgramType::Bool);
        Refused(context.declaration, "caster.alive && caster.attack", ProgramType::Bool, 17, {"'&&'", "right one is an int", "!= 0"});
        Refused(context.declaration, "caster.attack || caster.alive", ProgramType::Bool, 1, {"'||'", "left one is an int"});
        Refused(context.declaration, "caster.alive && 1", ProgramType::Bool, 17, {"right one is an int"});
        Refused(context.declaration, "caster.speed && 1.5", ProgramType::Bool, 1, {"left one is a float"});
    }

    TEST_F(ProgramCheckerTest, ADivisionOrModuloByALiteralZeroIsAnError) {
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

    TEST_F(ProgramCheckerTest, ACallNeedsAKnownFunctionAndItsArguments) {
        const CheckerContext context;
        Refused(context.declaration, "min(caster.attack)", ProgramType::Int32, 1, {"'min' takes 2 arguments", "1 is given"});
        Refused(context.declaration, "abs(1, 2)", ProgramType::Int32, 1, {"'abs' takes 1 argument,", "2 are given"});
        Refused(context.declaration, "select(1)", ProgramType::Int32, 1, {"'select' takes 3"});
        Refused(context.declaration, "round()", ProgramType::Float, 1, {"takes 1 argument", "0 are given"});
        const ProgramError unknown = Refused(context.declaration, "frobnicate(1)", ProgramType::Int32, 1, {"unknown function 'frobnicate'", "min", "clamp", "double", "color, at"});
        EXPECT_FALSE(unknown.message.empty());
        Refused(context.declaration, "caster(1)", ProgramType::Int32, 1, {"'caster' is a level, not a function"});
        Refused(context.declaration, "caster.attack(1)", ProgramType::Int32, 1, {"unknown function 'caster.attack'"});
    }

    TEST_F(ProgramCheckerTest, ANameThatIsNeitherALevelNorAFunctionIsUnknownAndListsWhatIsDeclared) {
        const CheckerContext context;
        Refused(context.declaration, "frob", ProgramType::Int32, 1, {"unknown name 'frob'", "declared levels are: caster, cast, behind", "functions are"});
        Refused(context.declaration, "1 + castr", ProgramType::Int32, 5, {"unknown name 'castr'", "did you mean 'caster'"});
        Refused(context.declaration, "caster", ProgramType::Int32, 1, {"'caster' is a level", "field"});
        Refused(context.declaration, "min", ProgramType::Int32, 1, {"'min' is a function", "min(...)"});
        Refused(context.declaration, "at", ProgramType::Int32, 1, {"'at' is a function", "at(...)"});
        const ContextDecl empty;
        Refused(empty, "caster.attack", ProgramType::Int32, 1, {"unknown level 'caster'"});
        Refused(empty, "x", ProgramType::Int32, 1, {"declared levels are: none"});
    }

    TEST_F(ProgramCheckerTest, APathThatLinkSlotRefusesKeepsItsMessageAndThePositionOfTheName) {
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

    TEST_F(ProgramCheckerTest, AFieldOfAnotherTypeIsNamedWithItsTypeAndWhatAFormulaReads) {
        const CheckerContext context;
        Refused(context.declaration, "caster.count", ProgramType::Int32, 1, {"caster.count", "uint32", "int, int64, float, double, vec2, vec3, vec4, ivec2, quat, color, enum"});
        Refused(context.declaration, "caster.spawn.x + 1", ProgramType::Float, 1, {"caster.spawn.x"});
        Refused(context.declaration, "caster.span", ProgramType::Int32, 1, {"ValueSpan", "reads only fields of the types"});
        Refused(context.declaration, "caster.bounds", ProgramType::Int32, 1, {"Rect", "reads only fields of the types"});
        Refused(context.declaration, "caster.spawn", ProgramType::Int32, 1, {"ValuePoint"});
        Refused(context.declaration, "caster.buffs", ProgramType::Int32, 1, {"list"});
        Refused(context.declaration, "behind.table", ProgramType::Int32, 1, {"map"});
    }

    TEST_F(ProgramCheckerTest, AFieldOfAnyProgramTypeIsASlotTypedWithItAlone) {
        const CheckerContext context;
        struct Row {
            ProgramType type;
            StringView path;
        };
        const Row rows[] = {{ProgramType::Vec2, "caster.position"}, {ProgramType::IVec2, "caster.cell"},  {ProgramType::Vec3, "caster.velocity"},
                            {ProgramType::Vec4, "caster.plane"},    {ProgramType::Quat, "caster.facing"}, {ProgramType::Color, "caster.tint"},
                            {ProgramType::String, "caster.title"},  {ProgramType::Vec3, "cast.target"},   {ProgramType::Vec2, "caster.span.offset"}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.path, row.type);
            ASSERT_EQ(expr.nodes.size(), 1U) << String(row.path).c_str();
            EXPECT_EQ(Root(expr).kind, TypedNodeKind::Slot);
            EXPECT_EQ(StringView(Root(expr).path), row.path);
            const auto linked = LinkSlot(context.declaration, row.path, DescriptorOf(row.type));
            ASSERT_TRUE(linked.has_value()) << String(row.path).c_str();
            EXPECT_EQ(Root(expr).slot.level, linked->level);
            EXPECT_EQ(Root(expr).slot.offset, linked->offset);

            // Read as another type it is refused by the type of the field, not by the path
            const ProgramType other = row.type == ProgramType::Bool ? ProgramType::Int32 : ProgramType::Bool;
            Refused(context.declaration, row.path, other, 1, {"the formula is", ProgramTypeName(row.type), "is asked for"});
        }
    }

    TEST_F(ProgramCheckerTest, AnEnumFieldIsASlotTypedWithItsOwnEnum) {
        const CheckerContext context;
        const TypedExpr element = AcceptedAs(context.declaration, "caster.element", DescribeType<ValueElement>());
        ASSERT_EQ(element.nodes.size(), 1U);
        EXPECT_EQ(Root(element).kind, TypedNodeKind::Slot);
        EXPECT_EQ(Root(element).type.program, ProgramType::Enum);
        ASSERT_NE(Root(element).type.enumeration, nullptr);
        EXPECT_EQ(Root(element).type.enumeration->enumBytes, sizeof(ValueElement));
        const auto linked = LinkSlot(context.declaration, "caster.element", DescribeType<ValueElement>());
        ASSERT_TRUE(linked.has_value());
        EXPECT_EQ(Root(element).slot.offset, linked->offset);

        AcceptedAs(context.declaration, "widths.narrow", DescribeType<ValueNarrowEnum>());
        AcceptedAs(context.declaration, "widths.wide16", DescribeType<ValueShortEnum>());
        AcceptedAs(context.declaration, "widths.wide32", DescribeType<ValueWideEnum>());
        RefusedAs(context.declaration, "caster.element", DescribeType<ValueNarrowEnum>(), 1, {"the formula is an enum {Fire, Water, Earth}", "a value of enum {Low, Mid, High}"});
        Refused(context.declaration, "caster.element", ProgramType::Int32, 1, {"the formula is an enum {Fire, Water, Earth}", "int"});
    }

    TEST_F(ProgramCheckerTest, EqualityWorksForEveryTypeAndOrderingAndArithmeticOnlyWhereTheTypeHasThem) {
        const CheckerContext context;
        const StringView fields[] = {"caster.position", "caster.cell", "caster.velocity", "caster.plane", "caster.facing", "caster.tint", "caster.title", "caster.element"};
        for (const StringView field : fields) {
            for (const StringView op : {"==", "!="}) {
                const String text = Format("{} {} {}", field, op, field);
                const TypedExpr expr = AcceptedAs(context.declaration, text, DescribeType<bool>());
                EXPECT_EQ(Root(expr).operandCount, 2U) << text.c_str();
            }
        }
        const TypedExpr position = Accepted(context.declaration, "caster.position == caster.position", ProgramType::Bool);
        EXPECT_EQ(Root(position).opcode, Opcode::EqVec2);
        const TypedExpr title = Accepted(context.declaration, "caster.title != caster.title", ProgramType::Bool);
        EXPECT_EQ(Root(title).opcode, Opcode::NeString);

        Refused(context.declaration, "caster.position < caster.position", ProgramType::Bool, 17, {"'<'", "not defined for vec2", "int, int64, float or double"});
        Refused(context.declaration, "caster.velocity >= caster.velocity", ProgramType::Bool, 17, {"'>='", "not defined for vec3"});
        Refused(context.declaration, "caster.tint + caster.tint", ProgramType::Color, 13, {"'+'", "not defined for color", "vec2, vec3, vec4 or ivec2"});
        Refused(context.declaration, "caster.element < caster.element", ProgramType::Bool, 16, {"not defined for enum {Fire, Water, Earth}"});
        Refused(context.declaration, "caster.title + caster.title", ProgramType::String, 14, {"'+'", "not defined for string"});
        Refused(context.declaration, "caster.alive && caster.title", ProgramType::Bool, 17, {"right one is a string"});
        // Types that have the row work: no per-type code in the checker
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.position + caster.position", ProgramType::Vec2)).opcode, Opcode::AddVec2);
        EXPECT_EQ(Root(Accepted(context.declaration, "-caster.velocity", ProgramType::Vec3)).opcode, Opcode::NegVec3);
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.facing * caster.facing", ProgramType::Quat)).opcode, Opcode::MulQuat);
        Refused(context.declaration, "-caster.title", ProgramType::String, 1, {"'-'", "not defined for string"});
        Refused(context.declaration, "min(caster.position, caster.position)", ProgramType::Vec2, 1, {"'min'", "not defined for vec2"});
    }

    TEST_F(ProgramCheckerTest, TwoTypesInOneOperatorAreNamedWithTheTypeOfTheEnumsToo) {
        const CheckerContext context;
        Refused(context.declaration, "caster.velocity == caster.position", ProgramType::Bool, 20, {"vec3 and vec2", "no implicit conversion"});
        Refused(context.declaration, "caster.title == caster.element", ProgramType::Bool, 17, {"string and enum {Fire, Water, Earth}"});
        // Two enums never mix, though the register image of both is an index
        const String same = "caster.element == widths.narrow";
        Refused(context.declaration, same, ProgramType::Bool, ColumnOf(same, "widths.narrow"), {"enum {Fire, Water, Earth} and enum {Low, Mid, High}"});
        Refused(context.declaration, "widths.narrow == widths.wide32", ProgramType::Bool, 18, {"enum {Low, Mid, High} and enum {Low, Mid, High}"});
        Accepted(context.declaration, "widths.narrow == widths.narrow", ProgramType::Bool);
    }

    TEST_F(ProgramCheckerTest, ANameBesideAnEnumIsItsEnumerator) {
        const CheckerContext context;
        for (const StringView text : {"caster.element == Fire", "Fire == caster.element", "caster.element != Earth", "Water != caster.element"}) {
            const TypedExpr expr = Accepted(context.declaration, text, ProgramType::Bool);
            ASSERT_EQ(expr.nodes.size(), 3U) << String(text).c_str();
            const TypedNode& slot = expr[0];
            const TypedNode& constant = expr[1];
            EXPECT_EQ(slot.kind, TypedNodeKind::Slot) << "the operand that reads the context is checked first: " << String(text).c_str();
            ASSERT_EQ(constant.kind, TypedNodeKind::Constant);
            EXPECT_TRUE(constant.type == slot.type) << "the enumerator has the enum of its neighbour";
        }
        EXPECT_EQ(ConstantOf<ProgramEnum>(Accepted(context.declaration, "caster.element == Fire", ProgramType::Bool)[1]).index, 0U);
        EXPECT_EQ(ConstantOf<ProgramEnum>(Accepted(context.declaration, "caster.element == Water", ProgramType::Bool)[1]).index, 1U);
        EXPECT_EQ(ConstantOf<ProgramEnum>(Accepted(context.declaration, "Earth == caster.element", ProgramType::Bool)[1]).index, 2U);
        EXPECT_EQ(ConstantOf<ProgramEnum>(AcceptedAs(context.declaration, "widths.wide32 == High", DescribeType<bool>())[1]).index, 2U) << "the enum of the field decides which names there are";

        // The expected type is a neighbour too
        const TypedExpr alone = AcceptedAs(context.declaration, "Earth", DescribeType<ValueElement>());
        ASSERT_EQ(alone.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<ProgramEnum>(alone[0]).index, 2U);
        const TypedExpr chosen = AcceptedAs(context.declaration, "select(caster.alive, Fire, Water)", DescribeType<ValueElement>());
        EXPECT_EQ(Root(chosen).opcode, Opcode::SelectEnum);
        EXPECT_EQ(ConstantOf<ProgramEnum>(chosen[1]).index, 0U);
        EXPECT_EQ(ConstantOf<ProgramEnum>(chosen[2]).index, 1U);
        const TypedExpr mixed = AcceptedAs(context.declaration, "select(cast.level > 2, Water, caster.element)", DescribeType<ValueElement>());
        EXPECT_EQ(Root(mixed).opcode, Opcode::SelectEnum);
        AcceptedAs(context.declaration, "select(caster.alive, Fire, caster.element) == Water", DescribeType<bool>());
    }

    TEST_F(ProgramCheckerTest, AnEnumeratorThatIsNotThereListsTheEnumeratorsAndTheNearestName) {
        const CheckerContext context;
        const String text = "caster.element == Fyre";
        Refused(context.declaration, text, ProgramType::Bool, ColumnOf(text, "Fyre"), {"unknown name 'Fyre'", "did you mean 'Fire'", "no enumerator of enum {Fire, Water, Earth}"});
        Refused(context.declaration, "caster.element == fire", ProgramType::Bool, 19, {"'fire'", "did you mean 'Fire'"});
        Refused(context.declaration, "caster.element == Low", ProgramType::Bool, 19, {"'Low'", "enum {Fire, Water, Earth}"});
        RefusedAs(context.declaration, "Cold", DescribeType<ValueElement>(), 1, {"'Cold'", "{Fire, Water, Earth}"});
    }

    TEST_F(ProgramCheckerTest, ANameWithNoEnumBesideItKeepsTheUnknownNameErrorAndSaysWhatAnEnumeratorNeeds) {
        const CheckerContext context;
        Refused(context.declaration, "Fire == Water", ProgramType::Bool, 1, {"unknown name 'Fire'", "declared levels are: caster, cast, behind, widths", "needs an enum beside it"});
        Refused(context.declaration, "caster.attack == Fire", ProgramType::Bool, 18, {"unknown name 'Fire'", "needs an enum beside it"});
        Refused(context.declaration, "Fire", ProgramType::Int32, 1, {"unknown name 'Fire'", "needs an enum beside it"});
        Refused(context.declaration, "select(caster.alive, Fire, Water)", ProgramType::Int32, 22, {"unknown name 'Fire'"});
    }

    TEST_F(ProgramCheckerTest, ALevelOrAFunctionWinsOverAnEnumeratorOfTheSameName) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ASSERT_TRUE(declaration.Add<ValueCastInfo>("Fire").has_value());
        Refused(declaration, "caster.element == Fire", ProgramType::Bool, 19, {"'Fire' is a level", "level.field"});
        Accepted(declaration, "caster.element == Water", ProgramType::Bool);
        Accepted(declaration, "Fire.level > 1", ProgramType::Bool);
    }

    TEST_F(ProgramCheckerTest, AStringLiteralIsAStringConstantOfThePool) {
        const CheckerContext context;
        const TypedExpr alone = Accepted(context.declaration, "'burn'", ProgramType::String);
        ASSERT_EQ(alone.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<PoolString>(alone[0]), "burn"_intern);
        EXPECT_EQ(ConstantOf<PoolString>(Accepted(context.declaration, "''", ProgramType::String)[0]), PoolString{});

        const TypedExpr compared = Accepted(context.declaration, "caster.title == 'Hero'", ProgramType::Bool);
        EXPECT_EQ(Root(compared).opcode, Opcode::EqString);
        EXPECT_EQ(ConstantOf<PoolString>(compared[1]), "Hero"_intern);
        Accepted(context.declaration, "'Hero' != caster.title", ProgramType::Bool);
        Accepted(context.declaration, "caster.title == 'a b + c'", ProgramType::Bool);
        Accepted(context.declaration, "'a' == 'a'", ProgramType::Bool);
        EXPECT_EQ(Root(Accepted(context.declaration, "select(caster.alive, 'burn', 'chill')", ProgramType::String)).opcode, Opcode::SelectString);
        EXPECT_EQ(Root(Accepted(context.declaration, "select(caster.alive, caster.title, 'chill')", ProgramType::String)).opcode, Opcode::SelectString);
        Accepted(context.declaration, "select(caster.element == Fire, 'burn', 'chill')", ProgramType::String);

        Refused(context.declaration, "'a' < 'b'", ProgramType::Bool, 5, {"'<'", "not defined for string", "int, int64, float or double"});
        Refused(context.declaration, "'a' + 'b'", ProgramType::String, 5, {"'+'", "not defined for string"});
        Refused(context.declaration, "caster.title == 1", ProgramType::Bool, 17, {"string and int"});
        Refused(context.declaration, "'a' == 1", ProgramType::Bool, 8, {"string and int"});
        Refused(context.declaration, "'a'", ProgramType::Int32, 1, {"the formula is a string", "int"});
        Refused(context.declaration, "float('a')", ProgramType::Float, 7, {"'float' converts a number", "a string"});
    }

    TEST_F(ProgramCheckerTest, AFloatLiteralOfAFloatIsReadAsAFloatAndNotRoundedTwice) {
        const CheckerContext context;
        // Just above the middle of 1.0f and the next float: through a double it lands on the middle and then rounds to even, down
        const TypedExpr expr = Accepted(context.declaration, "caster.speed + 1.00000005960464477539062501", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(expr[1]), std::nextafter(1.0f, 2.0f));
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "caster.speed + 0.1", ProgramType::Float)[1]), 0.1f);
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "-0.1", ProgramType::Float)[0]), -0.1f);
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "caster.speed + 1e-50", ProgramType::Float)[1]), 0.0f) << "below the smallest float it is the nearest one";
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "caster.precise + 1.00000005960464477539062501", ProgramType::Double)[1]), 1.00000005960464477539062501);
    }

    TEST_F(ProgramCheckerTest, MembersOfNonVectorsAreRefused) {
        const CheckerContext context;
        Refused(context.declaration, "(caster.attack).x", ProgramType::Int32, 16, {"'.x'", "an int has no components"});
        Refused(context.declaration, "min(caster.attack, 1).y + 1", ProgramType::Int32, 22, {"'.y'"});
    }

    TEST_F(ProgramCheckerTest, AnErrorColumnPointsAtTheNodeThatIsWrong) {
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

    // Components, Pack, mixed pairs and the vector functions

    namespace {

        struct ComponentRow {
            ProgramType type;
            StringView path;
            Array<StringView, 4> names;
            uint32_t count;
        };

        const ComponentRow kComponentRows[] = {{ProgramType::Vec2, "caster.position", {"x", "y", "", ""}, 2},  {ProgramType::IVec2, "caster.cell", {"x", "y", "", ""}, 2},
                                               {ProgramType::Vec3, "caster.velocity", {"x", "y", "z", ""}, 3}, {ProgramType::Vec4, "caster.plane", {"x", "y", "z", "w"}, 4},
                                               {ProgramType::Quat, "caster.facing", {"x", "y", "z", "w"}, 4},  {ProgramType::Color, "caster.tint", {"r", "g", "b", "a"}, 4},
                                               {ProgramType::Vec3, "cast.target", {"x", "y", "z", ""}, 3},     {ProgramType::Vec2, "caster.span.offset", {"x", "y", "", ""}, 2}};

        /// The program type a component of @p type has: `int` for a `Color` channel and an `ivec2` component, else `float`
        ProgramType ComponentTypeOf(ProgramType type) {
            return type == ProgramType::IVec2 || type == ProgramType::Color ? ProgramType::Int32 : ProgramType::Float;
        }

        uint32_t ByteOffsetOfComponent(ProgramType type, uint32_t index) {
            return type == ProgramType::Color ? index * static_cast<uint32_t>(sizeof(unsigned char)) : index * static_cast<uint32_t>(sizeof(float));
        }

        float FloatConstant(const TypedExpr& expr, TypedNodeId id) {
            return ConstantOf<float>(expr[id]);
        }

    }  // namespace

    TEST_F(ProgramCheckerTest, AComponentOfAFieldIsASlotAtTheComponentsOffsetAndAColorChannelAnExtract) {
        const CheckerContext context;
        for (const ComponentRow& row : kComponentRows) {
            const auto whole = LinkSlot(context.declaration, row.path, DescriptorOf(row.type));
            ASSERT_TRUE(whole.has_value()) << String(row.path).c_str();
            for (uint32_t i = 0; i < row.count; ++i) {
                const String text = Format("{}.{}", row.path, row.names[i]);
                const ProgramType componentType = ComponentTypeOf(row.type);
                const TypedExpr expr = Accepted(context.declaration, text, componentType);
                if (row.type == ProgramType::Color) {
                    ASSERT_EQ(expr.nodes.size(), 2U) << text.c_str();
                    EXPECT_EQ(expr[0].kind, TypedNodeKind::Slot);
                    EXPECT_EQ(expr[0].type, ProgramType::Color);
                    EXPECT_EQ(expr[0].slot.offset, whole->offset);
                    EXPECT_EQ(Root(expr).kind, TypedNodeKind::Operation);
                    EXPECT_EQ(Root(expr).opcode, Opcode::ExtractColor);
                    EXPECT_EQ(Root(expr).immediate, i) << text.c_str();
                    continue;
                }
                ASSERT_EQ(expr.nodes.size(), 1U) << text.c_str();
                EXPECT_EQ(Root(expr).kind, TypedNodeKind::Slot) << text.c_str();
                EXPECT_EQ(StringView(Root(expr).path), StringView(text)) << "the path stays as written";
                EXPECT_EQ(Root(expr).slot.level, whole->level);
                EXPECT_EQ(Root(expr).slot.offset, whole->offset + ByteOffsetOfComponent(row.type, i)) << text.c_str();
            }
        }
        // The offsets are the members' own
        const auto velocity = LinkSlot(context.declaration, "caster.velocity", DescribeType<Vec3>());
        ASSERT_TRUE(velocity.has_value());
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.velocity.y", ProgramType::Float)).slot.offset, velocity->offset + offsetof(Vec3, y));
        const auto plane = LinkSlot(context.declaration, "caster.plane", DescribeType<Vec4>());
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.plane.w", ProgramType::Float)).slot.offset, plane->offset + offsetof(Vec4, w));
        const auto cell = LinkSlot(context.declaration, "caster.cell", DescribeType<IVec2>());
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.cell.y", ProgramType::Int32)).slot.offset, cell->offset + offsetof(IVec2, y));
        const auto offset = LinkSlot(context.declaration, "caster.span.offset", DescribeType<Vec2>());
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.span.offset.y", ProgramType::Float)).slot.offset, offset->offset + offsetof(Vec2, y));
    }

    TEST_F(ProgramCheckerTest, AComponentOfAnExpressionIsAnExtractWithItsIndex) {
        const CheckerContext context;
        for (const ComponentRow& row : kComponentRows) {
            for (uint32_t i = 0; i < row.count; ++i) {
                const String text = Format("({}).{}", row.path, row.names[i]);
                const TypedExpr expr = Accepted(context.declaration, text, ComponentTypeOf(row.type));
                EXPECT_EQ(Root(expr).kind, TypedNodeKind::Operation) << text.c_str();
                EXPECT_EQ(Root(expr).opcode, OpcodeOf(ProgramOperation::Extract, row.type)) << text.c_str();
                EXPECT_EQ(Root(expr).immediate, i) << text.c_str();
                EXPECT_EQ(Root(expr).operandCount, 1U);
                EXPECT_EQ(expr[Root(expr).operands[0]].type, row.type);
            }
        }
        const TypedExpr blended = Accepted(context.declaration, "lerp(caster.velocity, cast.target, 0.5).y", ProgramType::Float);
        EXPECT_EQ(Root(blended).opcode, Opcode::ExtractVec3);
        EXPECT_EQ(Root(blended).immediate, 1U);
        const TypedExpr sum = Accepted(context.declaration, "(caster.velocity + cast.target).z + 1", ProgramType::Float);
        EXPECT_EQ(Root(sum).opcode, Opcode::AddFloat);
        const TypedExpr product = Accepted(context.declaration, "caster.velocity.x * length(cast.target)", ProgramType::Float);
        EXPECT_EQ(Root(product).opcode, Opcode::MulFloat);
    }

    TEST_F(ProgramCheckerTest, ANameThatIsNotAComponentSaysWhatTheTypeHas) {
        const CheckerContext context;
        Refused(context.declaration, "caster.velocity.q", ProgramType::Float, 1, {"'.q' is not a component of vec3", "it has x, y, z"});
        Refused(context.declaration, "caster.plane.v", ProgramType::Float, 1, {"not a component of vec4", "x, y, z, w"});
        Refused(context.declaration, "caster.tint.x", ProgramType::Int32, 1, {"not a component of color", "r, g, b, a"});
        Refused(context.declaration, "caster.position.z", ProgramType::Float, 1, {"not a component of vec2", "x, y"});
        Refused(context.declaration, "(caster.velocity).w", ProgramType::Float, 18, {"'.w'", "x, y, z"});
        Refused(context.declaration, "caster.attack.x", ProgramType::Int32, 1, {"'.x'", "an int has no components", "vec2, vec3, vec4, ivec2, quat or color"});
        Refused(context.declaration, "caster.velocity.x.y", ProgramType::Float, 1, {"caster.velocity.x.y", "no members"});
        Refused(context.declaration, "caster.element.x", ProgramType::Int32, 1, {"'.x'", "an enum {Fire, Water, Earth} has no components"});
        Refused(context.declaration, "caster.title.length", ProgramType::Int32, 1, {"a string has no components"});
        Refused(context.declaration, "(1 < 2).x", ProgramType::Float, 8, {"a bool has no components"});
        // A path that is wrong before its last segment keeps the message of the whole path
        Refused(context.declaration, "caster.nope.x", ProgramType::Float, 1, {"caster.nope.x", "has no field 'nope'"});
        Refused(context.declaration, "caster.span.nope.x", ProgramType::Float, 1, {"nope"});
        Refused(context.declaration, "hero.velocity.x", ProgramType::Float, 1, {"unknown level 'hero'"});
        Refused(context.declaration, "caster.span.low.x", ProgramType::Float, 1, {"an int has no components"});
        Refused(context.declaration, "behind.spans.x", ProgramType::Float, 1, {"behind.spans.x"});
    }

    TEST_F(ProgramCheckerTest, EveryPackBuildsItsTypeFromTheComponentsInOrder) {
        const CheckerContext context;
        struct Row {
            StringView text;
            ProgramType type;
            uint32_t count;
        };
        const Row rows[] = {{"vec2(1, 2)", ProgramType::Vec2, 2},   {"vec3(1, 2, 3)", ProgramType::Vec3, 3},    {"vec4(1, 2, 3, 4)", ProgramType::Vec4, 4},
                            {"ivec2(1, 2)", ProgramType::IVec2, 2}, {"quat(0, 0, 0, 1)", ProgramType::Quat, 4}, {"color(255, 0, 0, 255)", ProgramType::Color, 4}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.type);
            EXPECT_EQ(Root(expr).opcode, OpcodeOf(ProgramOperation::Pack, row.type)) << String(row.text).c_str();
            ASSERT_EQ(Root(expr).operandCount, row.count) << String(row.text).c_str();
            for (uint32_t i = 0; i < row.count; ++i) {
                EXPECT_EQ(expr[Root(expr).operands[i]].type, ComponentTypeOf(row.type)) << String(row.text).c_str();
            }
        }
        const TypedExpr three = Accepted(context.declaration, "vec3(1, 2, 3)", ProgramType::Vec3);
        EXPECT_EQ(FloatConstant(three, Root(three).operands[2]), 3.0f);
        const TypedExpr mixed = Accepted(context.declaration, "vec3(caster.speed, cast.power * 2, 1.5)", ProgramType::Vec3);
        EXPECT_EQ(Root(mixed).operandCount, 3U);
        const TypedExpr colored = Accepted(context.declaration, "color(caster.attack, cast.level * 2, 0, 255)", ProgramType::Color);
        EXPECT_EQ(Root(colored).operandCount, 4U);
        const TypedExpr nested = Accepted(context.declaration, "vec4(vec2(1, 2).x, 0, 0, vec3(1, 2, 3).z)", ProgramType::Vec4);
        EXPECT_EQ(Root(nested).operandCount, 4U);
        EXPECT_EQ(Root(Accepted(context.declaration, "vec3(1, 2, 3).y", ProgramType::Float)).opcode, Opcode::ExtractVec3);
    }

    TEST_F(ProgramCheckerTest, APackWithTheWrongArgumentsSaysWhatItNeeds) {
        const CheckerContext context;
        Refused(context.declaration, "vec3(1, 2)", ProgramType::Vec3, 1, {"'vec3' takes 3 arguments", "2 are given"});
        Refused(context.declaration, "quat(1, 2, 3, 4, 5)", ProgramType::Quat, 1, {"'quat' takes 4 arguments", "5 are given"});
        Refused(context.declaration, "vec3()", ProgramType::Vec3, 1, {"takes 3 arguments", "0 are given"});
        Refused(context.declaration, "vec3(caster.attack, 1, 2)", ProgramType::Vec3, 6, {"'vec3' takes float arguments", "argument 1 is an int", "float(x)"});
        Refused(context.declaration, "ivec2(1, caster.speed)", ProgramType::IVec2, 10, {"takes int arguments", "argument 2 is a float", "int(x)"});
        Refused(context.declaration, "vec2(caster.alive, 1)", ProgramType::Vec2, 6, {"argument 1 is a bool"});
        Refused(context.declaration, "vec3(caster.velocity, 1, 2)", ProgramType::Vec3, 6, {"argument 1 is a vec3"});
        Refused(context.declaration, "ivec2(1.5, 2)", ProgramType::IVec2, 7, {"1.5", "float literal", "an int is needed"});
        Refused(context.declaration, "color(1, 2, 3, 0.5)", ProgramType::Color, 16, {"0.5", "float literal"});
        Refused(context.declaration, "vec3(1, 2, 3)", ProgramType::Vec2, 1, {"the formula is a vec3", "vec2"});
        Refused(context.declaration, "vec3", ProgramType::Vec3, 1, {"'vec3' is a function"});
    }

    TEST_F(ProgramCheckerTest, TwoTypesOfAMixedPairUseItsRow) {
        const CheckerContext context;
        struct Row {
            StringView text;
            Opcode opcode;
            ProgramType type;
        };
        const Row rows[] = {{"caster.velocity * cast.power", Opcode::MulVec3Float, ProgramType::Vec3}, {"cast.power * caster.velocity", Opcode::MulFloatVec3, ProgramType::Vec3},
                            {"caster.velocity / cast.power", Opcode::DivVec3Float, ProgramType::Vec3}, {"caster.position * cast.power", Opcode::MulVec2Float, ProgramType::Vec2},
                            {"caster.plane * cast.power", Opcode::MulVec4Float, ProgramType::Vec4},    {"cast.power * caster.plane", Opcode::MulFloatVec4, ProgramType::Vec4},
                            {"caster.plane / cast.power", Opcode::DivVec4Float, ProgramType::Vec4},    {"caster.cell * cast.level", Opcode::MulIVec2Int32, ProgramType::IVec2},
                            {"cast.level * caster.cell", Opcode::MulInt32IVec2, ProgramType::IVec2},   {"caster.facing * cast.target", Opcode::MulQuatVec3, ProgramType::Vec3}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.type);
            EXPECT_EQ(Root(expr).opcode, row.opcode) << String(row.text).c_str();
            EXPECT_EQ(Root(expr).operandCount, 2U);
        }
        // The pair in the order of the text: the left operand is the first one whichever was checked first
        const TypedExpr scaled = Accepted(context.declaration, "cast.power * caster.velocity", ProgramType::Vec3);
        EXPECT_EQ(scaled[Root(scaled).operands[1]].path, "caster.velocity");
        EXPECT_EQ(scaled[Root(scaled).operands[0]].path, "cast.power");
        // Same-type rows are not mixed
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.facing * caster.facing", ProgramType::Quat)).opcode, Opcode::MulQuat);
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.velocity + cast.target", ProgramType::Vec3)).opcode, Opcode::AddVec3);
    }

    TEST_F(ProgramCheckerTest, ALiteralBesideAVectorTakesTheScalarOfThePair) {
        const CheckerContext context;
        struct Row {
            StringView text;
            Opcode opcode;
            ProgramType type;
            uint32_t literalOperand;
        };
        const Row rows[] = {{"caster.velocity * 2", Opcode::MulVec3Float, ProgramType::Vec3, 1},       {"2 * caster.velocity", Opcode::MulFloatVec3, ProgramType::Vec3, 0},
                            {"caster.velocity / 2", Opcode::DivVec3Float, ProgramType::Vec3, 1},       {"caster.velocity * 2.5", Opcode::MulVec3Float, ProgramType::Vec3, 1},
                            {"caster.cell * 2", Opcode::MulIVec2Int32, ProgramType::IVec2, 1},         {"2 * caster.cell", Opcode::MulInt32IVec2, ProgramType::IVec2, 0},
                            {"caster.velocity * (1 + 2)", Opcode::MulVec3Float, ProgramType::Vec3, 1}, {"(1 + 2) * caster.velocity", Opcode::MulFloatVec3, ProgramType::Vec3, 0},
                            {"caster.velocity * -2", Opcode::MulVec3Float, ProgramType::Vec3, 1}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.type);
            EXPECT_EQ(Root(expr).opcode, row.opcode) << String(row.text).c_str();
            const TypedNode& literal = expr[Root(expr).operands[row.literalOperand]];
            EXPECT_EQ(literal.type, row.type == ProgramType::IVec2 ? ProgramType::Int32 : ProgramType::Float) << String(row.text).c_str();
        }
        EXPECT_EQ(FloatConstant(Accepted(context.declaration, "caster.velocity * 2", ProgramType::Vec3), 1), 2.0f);
        // A constant vector beside a literal: the vector is typed by its own call
        const TypedExpr folded = Accepted(context.declaration, "vec3(1, 2, 3) * 2", ProgramType::Vec3);
        EXPECT_EQ(Root(folded).opcode, Opcode::MulVec3Float);
        EXPECT_EQ(Root(Accepted(context.declaration, "2 * vec3(1.5, 2, 3)", ProgramType::Vec3)).opcode, Opcode::MulFloatVec3);
        // Beside an addition a literal is not a scalar of the pair
        Refused(context.declaration, "caster.velocity + 1", ProgramType::Vec3, 19, {"vec3 and int", "no implicit conversion"});
        Refused(context.declaration, "caster.cell * 2.5", ProgramType::IVec2, 15, {"2.5", "float literal", "an int is needed"});
    }

    TEST_F(ProgramCheckerTest, APairWithoutARowIsRefusedAndListsTheMixedPairs) {
        const CheckerContext context;
        const String vectorByInt = "caster.velocity * caster.attack";
        Refused(context.declaration, vectorByInt, ProgramType::Vec3, ColumnOf(vectorByInt, "caster.attack"),
                {"'*'", "vec3 and int", "no implicit conversion", "mixed pairs", "vec3 * float", "float * vec3", "ivec2 * int", "quat * vec3"});
        // A rotation turns a vector, not the other way round
        const String backwards = "cast.target * caster.facing";
        Refused(context.declaration, backwards, ProgramType::Vec3, ColumnOf(backwards, "caster.facing"), {"vec3 and quat", "quat * vec3"});
        Refused(context.declaration, "caster.velocity * caster.position", ProgramType::Vec3, 19, {"vec3 and vec2"});
        Refused(context.declaration, "caster.cell * cast.power", ProgramType::IVec2, 15, {"ivec2 and float"});
        Refused(context.declaration, "caster.velocity / caster.velocity", ProgramType::Vec3, 17, {"'/'", "not defined for vec3", "mixed pairs", "vec3 / float"});
        Refused(context.declaration, "caster.velocity * caster.velocity", ProgramType::Vec3, 17, {"'*'", "not defined for vec3", "int, int64, float, double or quat"});
        Refused(context.declaration, "caster.cell / 2", ProgramType::IVec2, 13, {"'/'", "not defined for ivec2", "int, int64, float or double", "vec4 / float"});
        Refused(context.declaration, "caster.velocity % 2.0", ProgramType::Vec3, 17, {"'%'", "not defined for vec3", "int or int64"});
        Refused(context.declaration, "caster.position / 2", ProgramType::Vec2, 17, {"'/'", "not defined for vec2"});
        Refused(context.declaration, "caster.tint + caster.tint", ProgramType::Color, 13, {"'+'", "not defined for color"});
        Refused(context.declaration, "caster.facing + caster.facing", ProgramType::Quat, 15, {"'+'", "not defined for quat"});
        Refused(context.declaration, "caster.title + caster.title", ProgramType::String, 14, {"'+'", "not defined for string"});
        Refused(context.declaration, "caster.velocity < caster.velocity", ProgramType::Bool, 17, {"'<'", "not defined for vec3", "int, int64, float or double"});
        Refused(context.declaration, "caster.tint * 2", ProgramType::Color, 13, {"'*'", "not defined for color"});
    }

    TEST_F(ProgramCheckerTest, DotLengthAndNormalizeTakeTheResultTypeFromTheirRow) {
        const CheckerContext context;
        const TypedExpr dot = Accepted(context.declaration, "dot(caster.velocity, cast.target)", ProgramType::Float);
        EXPECT_EQ(Root(dot).opcode, Opcode::DotVec3);
        EXPECT_EQ(Root(dot).type, ProgramType::Float);
        EXPECT_EQ(Root(Accepted(context.declaration, "dot(caster.plane, caster.plane)", ProgramType::Float)).opcode, Opcode::DotVec4);
        EXPECT_EQ(Root(Accepted(context.declaration, "dot(caster.facing, caster.facing)", ProgramType::Float)).opcode, Opcode::DotQuat);
        const TypedExpr length = Accepted(context.declaration, "length(cast.target - caster.velocity)", ProgramType::Float);
        EXPECT_EQ(Root(length).opcode, Opcode::LengthVec3);
        EXPECT_EQ(length[Root(length).operands[0]].type, ProgramType::Vec3);
        EXPECT_EQ(Root(Accepted(context.declaration, "length(caster.plane)", ProgramType::Float)).opcode, Opcode::LengthVec4);
        EXPECT_EQ(Root(Accepted(context.declaration, "normalize(caster.velocity)", ProgramType::Vec3)).opcode, Opcode::NormalizeVec3);
        EXPECT_EQ(Root(Accepted(context.declaration, "normalize(caster.plane)", ProgramType::Vec4)).opcode, Opcode::NormalizeVec4);
        EXPECT_EQ(Root(Accepted(context.declaration, "normalize(caster.facing)", ProgramType::Quat)).opcode, Opcode::NormalizeQuat);
        // A float result goes on as a float
        EXPECT_EQ(Root(Accepted(context.declaration, "length(cast.target) * 2", ProgramType::Float)).opcode, Opcode::MulFloat);
        Refused(context.declaration, "length(cast.target)", ProgramType::Vec3, 1, {"the formula is a float", "vec3"});
        Refused(context.declaration, "dot(caster.position, caster.position)", ProgramType::Float, 1, {"'dot'", "not defined for vec2", "vec3, vec4 or quat"});
        Refused(context.declaration, "length(caster.position)", ProgramType::Float, 1, {"'length'", "not defined for vec2", "vec3 or vec4"});
        Refused(context.declaration, "length(caster.facing)", ProgramType::Float, 1, {"'length'", "not defined for quat"});
        Refused(context.declaration, "normalize(caster.position)", ProgramType::Vec2, 1, {"'normalize'", "not defined for vec2", "vec3, vec4 or quat"});
        Refused(context.declaration, "normalize(caster.attack)", ProgramType::Int32, 1, {"'normalize'", "not defined for int"});
        Refused(context.declaration, "dot(caster.velocity, caster.plane)", ProgramType::Float, 22, {"'dot'", "vec3 and vec4"});
        Refused(context.declaration, "dot(caster.velocity)", ProgramType::Float, 1, {"'dot' takes 2 arguments", "1 is given"});
    }

    TEST_F(ProgramCheckerTest, LerpTakesAWeightThatIsAFloatForAVectorAndTheTypeForAScalar) {
        const CheckerContext context;
        struct Row {
            StringView text;
            Opcode opcode;
            ProgramType type;
        };
        const Row rows[] = {{"lerp(caster.speed, caster.speed, 0.5)", Opcode::LerpFloat, ProgramType::Float},
                            {"lerp(caster.precise, caster.precise, 0.5)", Opcode::LerpDouble, ProgramType::Double},
                            {"lerp(caster.position, caster.position, 0.5)", Opcode::LerpVec2, ProgramType::Vec2},
                            {"lerp(caster.velocity, cast.target, 0.5)", Opcode::LerpVec3, ProgramType::Vec3},
                            {"lerp(caster.plane, caster.plane, cast.power)", Opcode::LerpVec4, ProgramType::Vec4},
                            {"lerp(caster.tint, color(0, 0, 0, 255), 0.5)", Opcode::LerpColor, ProgramType::Color},
                            {"lerp(caster.facing, quat(0, 0, 0, 1), cast.power)", Opcode::LerpQuat, ProgramType::Quat},
                            {"lerp(caster.velocity, cast.target, 1)", Opcode::LerpVec3, ProgramType::Vec3},
                            {"lerp(caster.precise, 1, 0.25)", Opcode::LerpDouble, ProgramType::Double},
                            {"lerp(0, 10, caster.speed)", Opcode::LerpFloat, ProgramType::Float},
                            {"lerp(0, 10, caster.precise)", Opcode::LerpDouble, ProgramType::Double}};
        for (const Row& row : rows) {
            const TypedExpr expr = Accepted(context.declaration, row.text, row.type);
            EXPECT_EQ(Root(expr).opcode, row.opcode) << String(row.text).c_str();
            ASSERT_EQ(Root(expr).operandCount, 3U) << String(row.text).c_str();
        }
        const TypedExpr weight = Accepted(context.declaration, "lerp(caster.velocity, cast.target, 1)", ProgramType::Vec3);
        EXPECT_EQ(weight[Root(weight).operands[2]].type, ProgramType::Float);
        Refused(context.declaration, "lerp(caster.cell, caster.cell, 0.5)", ProgramType::IVec2, 1, {"'lerp'", "not defined for ivec2", "float, double"});
        Refused(context.declaration, "lerp(caster.attack, caster.attack, 0.5)", ProgramType::Int32, 1, {"'lerp'", "not defined for int"});
        Refused(context.declaration, "lerp(caster.velocity, cast.target, caster.precise)", ProgramType::Vec3, 36, {"third argument", "blends vec3", "a double", "float(x)"});
        Refused(context.declaration, "lerp(caster.velocity, cast.target, caster.attack)", ProgramType::Vec3, 36, {"third argument", "an int"});
        Refused(context.declaration, "lerp(caster.speed, caster.speed, caster.precise)", ProgramType::Float, 34, {"'lerp'", "float and double"});
        Refused(context.declaration, "lerp(caster.velocity, caster.position, 0.5)", ProgramType::Vec3, 23, {"'lerp'", "vec3 and vec2"});
    }

    TEST_F(ProgramCheckerTest, EveryOperatorAndFunctionIsAcceptedExactlyForTheTypesThatHaveARow) {
        const CheckerContext context;
        struct Field {
            ProgramType type;
            StringView path;
        };
        const Field fields[] = {{ProgramType::Bool, "caster.alive"},     {ProgramType::Int32, "caster.attack"},  {ProgramType::Int64, "caster.score"},   {ProgramType::Float, "caster.speed"},
                                {ProgramType::Double, "caster.precise"}, {ProgramType::Vec2, "caster.position"}, {ProgramType::Vec3, "caster.velocity"}, {ProgramType::Vec4, "caster.plane"},
                                {ProgramType::IVec2, "caster.cell"},     {ProgramType::Quat, "caster.facing"},   {ProgramType::Color, "caster.tint"},    {ProgramType::Enum, "caster.element"},
                                {ProgramType::String, "caster.title"}};
        // Every ProgramOperation that has a text form, as the operator or the function that spells it
        struct Spelling {
            ProgramOperation operation;
            StringView symbol;
            uint32_t arguments;
            bool infix;
        };
        const Spelling spellings[] = {{ProgramOperation::Add, "+", 2, true},          {ProgramOperation::Sub, "-", 2, true},
                                      {ProgramOperation::Mul, "*", 2, true},          {ProgramOperation::Div, "/", 2, true},
                                      {ProgramOperation::Mod, "%", 2, true},          {ProgramOperation::Eq, "==", 2, true},
                                      {ProgramOperation::Ne, "!=", 2, true},          {ProgramOperation::Lt, "<", 2, true},
                                      {ProgramOperation::Le, "<=", 2, true},          {ProgramOperation::Gt, ">", 2, true},
                                      {ProgramOperation::Ge, ">=", 2, true},          {ProgramOperation::And, "&&", 2, true},
                                      {ProgramOperation::Or, "||", 2, true},          {ProgramOperation::Neg, "-", 1, false},
                                      {ProgramOperation::Not, "!", 1, false},         {ProgramOperation::Abs, "abs", 1, false},
                                      {ProgramOperation::Min, "min", 2, false},       {ProgramOperation::Max, "max", 2, false},
                                      {ProgramOperation::Clamp, "clamp", 3, false},   {ProgramOperation::Floor, "floor", 1, false},
                                      {ProgramOperation::Ceil, "ceil", 1, false},     {ProgramOperation::Round, "round", 1, false},
                                      {ProgramOperation::Sqrt, "sqrt", 1, false},     {ProgramOperation::Dot, "dot", 2, false},
                                      {ProgramOperation::Length, "length", 1, false}, {ProgramOperation::Normalize, "normalize", 1, false}};
        for (const Spelling& spelling : spellings) {
            for (const Field& field : fields) {
                String text;
                if (spelling.infix) {
                    text = Format("{} {} {}", field.path, spelling.symbol, field.path);
                } else if (spelling.arguments == 1 && (spelling.operation == ProgramOperation::Neg || spelling.operation == ProgramOperation::Not)) {
                    text = Format("{}{}", spelling.symbol, field.path);
                } else {
                    text = Format("{}(", spelling.symbol);
                    for (uint32_t i = 0; i < spelling.arguments; ++i) {
                        text += Format("{}{}", i == 0 ? "" : ", ", field.path);
                    }
                    text += ")";
                }
                const Opcode opcode = OpcodeOf(spelling.operation, field.type);
                if (opcode != Opcode::Invalid) {
                    const ProgramType result = *ResultTypeOf(opcode);
                    const TypedExpr expr = Accepted(context.declaration, text, result);
                    EXPECT_EQ(Root(expr).opcode, opcode) << text.c_str();
                    continue;
                }
                const auto refused = Check(context.declaration, text, DescriptorOf(field.type));
                ASSERT_FALSE(refused.has_value()) << text.c_str() << " has no row and must be refused";
                const String& message = refused.error().message;
                EXPECT_TRUE(Contains(message, spelling.symbol)) << text.c_str() << ": " << message.c_str();
                EXPECT_TRUE(Contains(message, ProgramTypeName(field.type))) << text.c_str() << ": " << message.c_str();
                const bool logic = spelling.operation == ProgramOperation::And || spelling.operation == ProgramOperation::Or || spelling.operation == ProgramOperation::Not;
                if (!logic) {
                    EXPECT_TRUE(Contains(message, "it works on")) << text.c_str() << ": " << message.c_str();
                    for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
                        if (OpcodeOf(spelling.operation, type) != Opcode::Invalid) {
                            EXPECT_TRUE(Contains(message, ProgramTypeName(type))) << text.c_str() << ": " << message.c_str() << " does not list " << String(ProgramTypeName(type)).c_str();
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    // The deepest trees the limits allow
    // =========================================================================

    namespace {

        String Repeated(StringView part, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        /// What parsing, printing, checking and lowering give for @p text, collected on the thread that ran them: a gtest macro belongs to the test's own thread
        struct PipelineOutcome {
            bool parsed = false;
            bool printed = false;
            bool checked = false;
            bool compiled = false;
        };

        PipelineOutcome RunPipeline(StringView text, const ContextDecl& declaration) {
            PipelineOutcome outcome;
            const auto ast = ParseExpression(text);
            outcome.parsed = ast.has_value();
            if (!ast.has_value()) {
                return outcome;
            }
            outcome.printed = !PrintExpression(*ast).empty();
            outcome.checked = CheckExpression(*ast, declaration, DescribeType<int32_t>()).has_value();
            outcome.compiled = CompileExpression(text, declaration, DescribeType<int32_t>()).has_value();
            return outcome;
        }

        /// A thread has the smallest secondary-thread stack the engine runs on; without threads the caller's stack has to do
        PipelineOutcome RunPipelineOnASmallStack(StringView text, const ContextDecl& declaration) {
            PipelineOutcome outcome;
            const auto run = [&] { outcome = RunPipeline(text, declaration); };
            if constexpr (Thread::kAvailable) {
                Thread thread("PipelineOnASmallStack", run);
                thread.join();
            } else {
                run();
            }
            return outcome;
        }

    }  // namespace

    /// The checker, the lowering and the printer recurse by the height of the tree, which the parser bounds by `ExprLimits::kMaxTreeHeight` for every shape of it: the highest trees
    /// that limit allows (a chain of operators, calls over a chain, unary operators over a chain, the deepest parentheses) must fit the stack of a secondary thread
    TEST_F(ProgramCheckerTest, TheHighestTreesTheLimitAllowsFitTheStackOfAThread) {
        constexpr size_t kLevels = ExprLimits::kMaxTreeHeight;
        const auto chain = [](size_t operators) { return Format("caster.attack{}", Repeated(" + caster.attack", operators)); };
        const String texts[] = {
            chain(kLevels),
            Format("{}{}{}", Repeated("abs(", kLevels / 2), chain(kLevels / 2 - 1), Repeated(")", kLevels / 2)),
            Format("-({})", chain(kLevels - 1)),
            Format("{}{}", Repeated("-", kLevels / 2), chain(kLevels / 2)),
            Format("{}caster.attack{}", Repeated("(", ExprLimits::kMaxNesting), Repeated(")", ExprLimits::kMaxNesting)),
            Format("{}caster.attack", Repeated("-", kLevels)),
        };
        const CheckerContext context;
        for (const String& text : texts) {
            const PipelineOutcome outcome = RunPipelineOnASmallStack(text, context.declaration);
            EXPECT_TRUE(outcome.parsed) << text.c_str();
            EXPECT_TRUE(outcome.printed) << text.c_str();
            EXPECT_TRUE(outcome.checked) << text.c_str();
            EXPECT_TRUE(outcome.compiled) << text.c_str();
        }
    }

    TEST_F(ProgramCheckerTest, AnInlineTableIsTypedByItsEntriesAndTakesAnIntIndex) {
        const CheckerContext context;
        const TypedExpr plain = Accepted(context.declaration, "at(cast.level, 10, 20, 30)", ProgramType::Int32);
        EXPECT_EQ(Root(plain).opcode, Opcode::LoadTable);
        EXPECT_EQ(Root(plain).operandCount, 1);
        ASSERT_EQ(Root(plain).entries.size(), 3U);
        EXPECT_EQ(ConstantOf<int32_t>(plain[Root(plain).entries[2]]), 30);
        EXPECT_EQ(Root(plain).type, ExprType(ProgramType::Int32));

        EXPECT_EQ(Root(Accepted(context.declaration, "at(cast.level, 1.5, 2)", ProgramType::Float)).type, ExprType(ProgramType::Float));
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.score + at(0, 1, 2)", ProgramType::Int64)).type, ExprType(ProgramType::Int64));
        EXPECT_EQ(Root(Accepted(context.declaration, "at(cast.level, vec3(1, 0, 0), caster.velocity)", ProgramType::Vec3)).type, ExprType(ProgramType::Vec3));
        EXPECT_EQ(Root(Accepted(context.declaration, "at(cast.level, 'a', 'b')", ProgramType::String)).type, ExprType(ProgramType::String));
        const TypedExpr elements = AcceptedAs(context.declaration, "at(cast.level, Fire, Water)", DescribeType<ValueElement>());
        EXPECT_EQ(Root(elements).type.program, ProgramType::Enum);
        EXPECT_EQ(ConstantOf<ProgramEnum>(elements[Root(elements).entries[1]]).index, 1U);
        // The operand that reads the context comes first, so the enumerators beside it are its enum's
        EXPECT_EQ(Root(Accepted(context.declaration, "caster.element == at(cast.level, Fire, Water)", ProgramType::Bool)).type, ExprType(ProgramType::Bool));
    }

    TEST_F(ProgramCheckerTest, AnInlineTableThatIsNotWellFormedIsRefusedAtTheNodeThatIsWrong) {
        const CheckerContext context;
        Refused(context.declaration, "at(cast.level)", ProgramType::Int32, 1, {"'at'", "an index and at least one entry", "1 argument is given"});
        Refused(context.declaration, "at()", ProgramType::Int32, 1, {"'at'", "an index and at least one entry", "0 arguments are given"});
        Refused(context.declaration, "at(caster.score, 1, 2)", ProgramType::Int32, 4, {"'at'", "an int as its index", "an int64", "int(x)"});
        Refused(context.declaration, "at(cast.power, 1, 2)", ProgramType::Int32, 4, {"an int as its index", "a float", "int(x)"});
        Refused(context.declaration, "at(1.5, 1, 2)", ProgramType::Int32, 4, {"float literal", "an int"});
        Refused(context.declaration, "at(caster.alive, 1, 2)", ProgramType::Int32, 4, {"an int as its index", "a bool"});
        Refused(context.declaration, "at(cast.level, 1, 2.5)", ProgramType::Int32, 19, {"2.5", "float literal", "an int"});
        Refused(context.declaration, "at(cast.level, 1, true)", ProgramType::Int32, 19, {"'at'", "operands of one type", "int and bool"});
        Refused(context.declaration, "at(cast.level, 'a', 2)", ProgramType::String, 21, {"operands of one type", "string and int"});
        Refused(context.declaration, "at(cast.level, vec3(1, 0, 0), 2)", ProgramType::Vec3, 31, {"operands of one type", "vec3 and int"});
        Refused(context.declaration, "at(cast.level, Fire, Mud)", ProgramType::Enum, 22, {"unknown name 'Mud'"});
        Refused(context.declaration, "at(cast.level, 1, 2)", ProgramType::Bool, 1, {"the formula is an int", "bool"});
        Refused(context.declaration, "at(cast.level, 1, 2) + 1.5", ProgramType::Int32, 24, {"float literal"});
    }

    TEST_F(ProgramCheckerTest, ATableOverTheLimitIsRefusedAndOneAtTheLimitIsNot) {
        const CheckerContext context;
        const auto table = [](size_t entries) {
            ExprAst ast;
            ExprNode index;
            index.kind = ExprNodeKind::Name;
            index.text = "cast.level";
            index.column = 4;
            ast.nodes.push_back(index);
            ast.arguments.push_back(0);
            for (size_t i = 0; i < entries; ++i) {
                ExprNode entry;
                entry.kind = ExprNodeKind::Number;
                entry.text = "1";
                entry.column = 1;
                ast.nodes.push_back(entry);
                ast.arguments.push_back(static_cast<ExprNodeId>(ast.nodes.size() - 1));
            }
            ExprNode call;
            call.kind = ExprNodeKind::Call;
            call.text = "at";
            call.column = 1;
            call.firstArgument = 0;
            call.argumentCount = static_cast<uint32_t>(ast.arguments.size());
            ast.nodes.push_back(call);
            ast.root = static_cast<ExprNodeId>(ast.nodes.size() - 1);
            return ast;
        };
        EXPECT_TRUE(CheckExpression(table(kMaxTableEntries), context.declaration, DescribeType<int32_t>()).has_value());
        const auto over = CheckExpression(table(kMaxTableEntries + 1), context.declaration, DescribeType<int32_t>());
        ASSERT_FALSE(over.has_value());
        EXPECT_EQ(over.error().column, 1U);
        EXPECT_TRUE(Contains(over.error().message, Format("{} entries", kMaxTableEntries + 1))) << over.error().message.c_str();
        EXPECT_TRUE(Contains(over.error().message, Format("at most {}", kMaxTableEntries))) << over.error().message.c_str();
    }

    TEST_F(ProgramCheckerTest, ANamedTableIsCopiedIntoConstantEntriesOfTheTypeBesideIt) {
        const CheckerContext context;
        ValueTableSet tables;
        tables.Add("Damage", {10.0, 20.0, 30.0});
        CompileOptions options;
        options.tables = &tables;
        const auto check = [&](StringView text, ProgramType expected) {
            const auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            return parsed.has_value() ? CheckExpression(*parsed, context.declaration, DescriptorOf(expected), options) : std::unexpected(parsed.error());
        };
        const auto asInt = check("ByLevel('Damage', cast.level)", ProgramType::Int32);
        ASSERT_TRUE(asInt.has_value()) << asInt.error().message.c_str();
        EXPECT_EQ(Root(*asInt).opcode, Opcode::LoadTable);
        EXPECT_EQ(Root(*asInt).type, ExprType(ProgramType::Int32));
        ASSERT_EQ(Root(*asInt).entries.size(), 3U);
        EXPECT_EQ(ConstantOf<int32_t>((*asInt)[Root(*asInt).entries[1]]), 20);
        EXPECT_EQ((*asInt)[Root(*asInt).operands[0]].path, "cast.level");

        const auto asDouble = check("ByLevel('Damage', 1) > 15", ProgramType::Bool);
        ASSERT_TRUE(asDouble.has_value()) << asDouble.error().message.c_str();
        const TypedNode& table = (*asDouble)[Root(*asDouble).operands[0]];
        EXPECT_EQ(table.type, ExprType(ProgramType::Double));
        EXPECT_EQ(ConstantOf<double>((*asDouble)[table.entries[2]]), 30.0);

        const auto beside = check("ByLevel('Damage', cast.level) + caster.speed", ProgramType::Float);
        ASSERT_TRUE(beside.has_value()) << beside.error().message.c_str();
        EXPECT_EQ(ConstantOf<float>((*beside)[(*beside)[Root(*beside).operands[0]].entries[0]]), 10.0f);

        // Without the provider of the options the same text is the one error, at the name of the call
        const auto parsed = ParseExpression("ByLevel('Damage', cast.level)");
        ASSERT_TRUE(parsed.has_value());
        const auto bare = CheckExpression(*parsed, context.declaration, DescribeType<int32_t>());
        ASSERT_FALSE(bare.has_value());
        EXPECT_EQ(bare.error().column, 1U);
        EXPECT_TRUE(Contains(bare.error().message, "no tables are declared where this formula is read")) << bare.error().message.c_str();
    }

}  // namespace BECore::Tests
