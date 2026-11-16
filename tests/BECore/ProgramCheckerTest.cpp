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
        Refused(context.declaration, "caster.alive + caster.alive", ProgramType::Bool, 14, {"'+'", "not defined for bool", "int, int64, float, double, vec2, vec3, vec4 or ivec2"});
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
        const auto rect = CheckExpression(*parsed, context.declaration, DescribeType<Rect>());
        ASSERT_FALSE(rect.has_value());
        EXPECT_EQ(rect.error().column, 0U);
        EXPECT_TRUE(Contains(rect.error().message, "bool, int, int64, float, double, vec2, vec3, vec4, ivec2, quat, color, enum or string")) << rect.error().message.c_str();
        EXPECT_TRUE(Contains(rect.error().message, "Rect")) << rect.error().message.c_str();
        const auto unsignedType = CheckExpression(*parsed, context.declaration, DescribeType<uint32_t>());
        ASSERT_FALSE(unsignedType.has_value());
        EXPECT_TRUE(Contains(unsignedType.error().message, "uint32")) << unsignedType.error().message.c_str();
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<String>()).has_value());
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<ValuePoint>()).has_value());
        EXPECT_FALSE(CheckExpression(*parsed, context.declaration, DescribeType<Vector<int32_t>>()).has_value());
    }

    TEST(ProgramCheckerTest, EveryProgramTypeAndEveryEnumIsAnExpectedType) {
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
        Refused(context.declaration, "caster.count", ProgramType::Int32, 1, {"caster.count", "uint32", "int, int64, float, double, vec2, vec3, vec4, ivec2, quat, color, enum or string"});
        Refused(context.declaration, "caster.velocity.x + 1", ProgramType::Float, 1, {"caster.velocity.x"});
        Refused(context.declaration, "caster.span", ProgramType::Int32, 1, {"ValueSpan", "reads only fields of the types"});
        Refused(context.declaration, "caster.bounds", ProgramType::Int32, 1, {"Rect", "reads only fields of the types"});
        Refused(context.declaration, "caster.spawn", ProgramType::Int32, 1, {"ValuePoint"});
        Refused(context.declaration, "caster.buffs", ProgramType::Int32, 1, {"list"});
        Refused(context.declaration, "behind.table", ProgramType::Int32, 1, {"map"});
    }

    TEST(ProgramCheckerTest, AFieldOfAnyProgramTypeIsASlotTypedWithItAlone) {
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
            EXPECT_EQ(Root(expr).path, String(row.path));
            const auto linked = LinkSlot(context.declaration, row.path, DescriptorOf(row.type));
            ASSERT_TRUE(linked.has_value()) << String(row.path).c_str();
            EXPECT_EQ(Root(expr).slot.level, linked->level);
            EXPECT_EQ(Root(expr).slot.offset, linked->offset);

            // Read as another type it is refused by the type of the field, not by the path
            const ProgramType other = row.type == ProgramType::Bool ? ProgramType::Int32 : ProgramType::Bool;
            Refused(context.declaration, row.path, other, 1, {"the formula is", ProgramTypeName(row.type), "is asked for"});
        }
    }

    TEST(ProgramCheckerTest, AnEnumFieldIsASlotTypedWithItsOwnEnum) {
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

    TEST(ProgramCheckerTest, EqualityWorksForEveryTypeAndOrderingAndArithmeticOnlyWhereTheTypeHasThem) {
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

    TEST(ProgramCheckerTest, TwoTypesInOneOperatorAreNamedWithTheTypeOfTheEnumsToo) {
        const CheckerContext context;
        Refused(context.declaration, "caster.velocity == caster.position", ProgramType::Bool, 20, {"vec3 and vec2", "no implicit conversion"});
        Refused(context.declaration, "caster.title == caster.element", ProgramType::Bool, 17, {"string and enum {Fire, Water, Earth}"});
        // Two enums never mix, though the register image of both is an index
        const String same = "caster.element == widths.narrow";
        Refused(context.declaration, same, ProgramType::Bool, ColumnOf(same, "widths.narrow"), {"enum {Fire, Water, Earth} and enum {Low, Mid, High}"});
        Refused(context.declaration, "widths.narrow == widths.wide32", ProgramType::Bool, 18, {"enum {Low, Mid, High} and enum {Low, Mid, High}"});
        Accepted(context.declaration, "widths.narrow == widths.narrow", ProgramType::Bool);
    }

    TEST(ProgramCheckerTest, ANameBesideAnEnumIsItsEnumerator) {
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

    TEST(ProgramCheckerTest, AnEnumeratorThatIsNotThereListsTheEnumeratorsAndTheNearestName) {
        const CheckerContext context;
        const String text = "caster.element == Fyre";
        Refused(context.declaration, text, ProgramType::Bool, ColumnOf(text, "Fyre"), {"unknown name 'Fyre'", "did you mean 'Fire'", "no enumerator of enum {Fire, Water, Earth}"});
        Refused(context.declaration, "caster.element == fire", ProgramType::Bool, 19, {"'fire'", "did you mean 'Fire'"});
        Refused(context.declaration, "caster.element == Low", ProgramType::Bool, 19, {"'Low'", "enum {Fire, Water, Earth}"});
        RefusedAs(context.declaration, "Cold", DescribeType<ValueElement>(), 1, {"'Cold'", "{Fire, Water, Earth}"});
    }

    TEST(ProgramCheckerTest, ANameWithNoEnumBesideItKeepsTheUnknownNameErrorAndSaysWhatAnEnumeratorNeeds) {
        const CheckerContext context;
        Refused(context.declaration, "Fire == Water", ProgramType::Bool, 1, {"unknown name 'Fire'", "declared levels are: caster, cast, behind, widths", "needs an enum beside it"});
        Refused(context.declaration, "caster.attack == Fire", ProgramType::Bool, 18, {"unknown name 'Fire'", "needs an enum beside it"});
        Refused(context.declaration, "Fire", ProgramType::Int32, 1, {"unknown name 'Fire'", "needs an enum beside it"});
        Refused(context.declaration, "select(caster.alive, Fire, Water)", ProgramType::Int32, 22, {"unknown name 'Fire'"});
    }

    TEST(ProgramCheckerTest, ALevelOrAFunctionWinsOverAnEnumeratorOfTheSameName) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ASSERT_TRUE(declaration.Add<ValueCastInfo>("Fire").has_value());
        Refused(declaration, "caster.element == Fire", ProgramType::Bool, 19, {"'Fire' is a level", "level.field"});
        Accepted(declaration, "caster.element == Water", ProgramType::Bool);
        Accepted(declaration, "Fire.level > 1", ProgramType::Bool);
    }

    TEST(ProgramCheckerTest, AStringLiteralIsAStringConstantOfThePool) {
        const CheckerContext context;
        const TypedExpr alone = Accepted(context.declaration, "'burn'", ProgramType::String);
        ASSERT_EQ(alone.nodes.size(), 1U);
        EXPECT_EQ(ConstantOf<PoolString>(alone[0]), PoolString::Intern("burn"));
        EXPECT_EQ(ConstantOf<PoolString>(Accepted(context.declaration, "''", ProgramType::String)[0]), PoolString{});

        const TypedExpr compared = Accepted(context.declaration, "caster.title == 'Hero'", ProgramType::Bool);
        EXPECT_EQ(Root(compared).opcode, Opcode::EqString);
        EXPECT_EQ(ConstantOf<PoolString>(compared[1]), PoolString::Intern("Hero"));
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

    TEST(ProgramCheckerTest, AFloatLiteralOfAFloatIsReadAsAFloatAndNotRoundedTwice) {
        const CheckerContext context;
        // Just above the middle of 1.0f and the next float: through a double it lands on the middle and then rounds to even, down
        const TypedExpr expr = Accepted(context.declaration, "caster.speed + 1.00000005960464477539062501", ProgramType::Float);
        EXPECT_EQ(ConstantOf<float>(expr[1]), std::nextafter(1.0f, 2.0f));
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "caster.speed + 0.1", ProgramType::Float)[1]), 0.1f);
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "-0.1", ProgramType::Float)[0]), -0.1f);
        EXPECT_EQ(ConstantOf<float>(Accepted(context.declaration, "caster.speed + 1e-50", ProgramType::Float)[1]), 0.0f) << "below the smallest float it is the nearest one";
        EXPECT_EQ(ConstantOf<double>(Accepted(context.declaration, "caster.precise + 1.00000005960464477539062501", ProgramType::Double)[1]), 1.00000005960464477539062501);
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
