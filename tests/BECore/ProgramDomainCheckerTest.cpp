// The type checker over the types and operations a game registered (Values/Programs/Domain/CLAUDE.md): a field of a registered type, the resolution of an operation by its
// argument count and types, the hints and the messages. The toy mask and its operations are those of `ProgramDomainTypes.h`.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/ScratchScopeFixture.h>
#include <Common/TestText.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using ProgramDomainCheckerTest = ScratchScopeFixture;

    using namespace ProgramDomainData;

    namespace {

        // Overloads that only the argument types tell apart, one name with two counts, and a type without bitwise equality as a result

        [[nodiscard]] int32_t MergeWithInt(const DomainBits256& mask, int32_t extra) {
            return BitsPopcount(mask) + extra;
        }

        [[nodiscard]] DomainBits256 MergeWithMask(const DomainBits256& mask, const DomainBits256& other) {
            return BitsOr(mask, other);
        }

        [[nodiscard]] int32_t CountOne(const DomainBits256& mask) {
            return BitsPopcount(mask);
        }

        [[nodiscard]] int32_t CountTwo(const DomainBits256& mask, int32_t from) {
            return BitsPopcount(mask) - from;
        }

        [[nodiscard]] DomainWeighted WeightedOf(int32_t count) {
            return DomainWeighted{1.0f, count};
        }

        struct CheckerToys {
            DomainOperationId mergeWithInt = 0;
            DomainOperationId mergeWithMask = 0;
        };

        const CheckerToys& RegisterCheckerToys() {
            static const CheckerToys toys = [] {
                static_cast<void>(RegisterDomainToys());
                CheckerToys made;
                made.mergeWithInt = Require(ProgramDomain::AddOperation<&MergeWithInt>("chk_merge"));
                made.mergeWithMask = Require(ProgramDomain::AddOperation<&MergeWithMask>("chk_merge"));
                static_cast<void>(Require(ProgramDomain::AddOperation<&CountOne>("chk_count")));
                static_cast<void>(Require(ProgramDomain::AddOperation<&CountTwo>("chk_count")));
                static_cast<void>(Require(ProgramDomain::AddOperation<&WeightedOf>("chk_weighted")));
                return made;
            }();
            return toys;
        }

        struct DomainCheckerContext {
            ContextDecl declaration;
            const DomainToys& toys = RegisterDomainToys();
            const CheckerToys& more = RegisterCheckerToys();

            DomainCheckerContext() {
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
            }

            [[nodiscard]] ExprType Mask() const {
                return ExprType::OfDomain(toys.mask);
            }
        };

        std::expected<TypedExpr, ProgramError> Check(const DomainCheckerContext& context, StringView text, const TypeDesc& expected) {
            const auto parsed = ParseExpression(text);
            EXPECT_TRUE(parsed.has_value()) << String(text).c_str();
            if (!parsed.has_value()) {
                return std::unexpected(parsed.error());
            }
            return CheckExpression(*parsed, context.declaration, expected);
        }

        TypedExpr Accepted(const DomainCheckerContext& context, StringView text, const TypeDesc& expected) {
            auto checked = Check(context, text, expected);
            EXPECT_TRUE(checked.has_value()) << String(text).c_str() << ": " << (checked.has_value() ? String{} : checked.error().message).c_str();
            return checked.has_value() ? std::move(*checked) : TypedExpr{};
        }

        /// The message must say every one of @p words, at @p column
        void Refused(const DomainCheckerContext& context, StringView text, const TypeDesc& expected, uint32_t column, std::initializer_list<StringView> words) {
            const auto checked = Check(context, text, expected);
            ASSERT_FALSE(checked.has_value()) << String(text).c_str();
            EXPECT_EQ(checked.error().column, column) << String(text).c_str() << ": " << checked.error().message.c_str();
            for (const StringView word : words) {
                EXPECT_TRUE(Contains(checked.error().message, word)) << String(text).c_str() << ": " << checked.error().message.c_str() << " does not say '" << String(word).c_str() << "'";
            }
        }

        const TypeDesc& MaskType() {
            return DescribeType<DomainBits256>();
        }

        const TypedNode& Root(const TypedExpr& expr) {
            return expr[expr.root];
        }

        template <typename T>
        T ConstantOf(const TypedNode& node) {
            T value{};
            std::memcpy(&value, node.constant.bytes.data(), sizeof(T));
            return value;
        }

    }  // namespace

    TEST_F(ProgramDomainCheckerTest, AFieldOfARegisteredTypeIsASlotOfTypeDomainAndAFormulaMayHaveThatType) {
        const DomainCheckerContext context;
        const TypedExpr expr = Accepted(context, "board.walls", MaskType());
        EXPECT_EQ(Root(expr).kind, TypedNodeKind::Slot);
        EXPECT_EQ(Root(expr).type, context.Mask());
        EXPECT_EQ(Root(expr).type.domain, context.toys.mask);
        EXPECT_EQ(expr.type, context.Mask());
        // Another registered type is another type, whatever its size
        const TypedExpr pair = Accepted(context, "board.score", DescribeType<DomainPair>());
        EXPECT_NE(Root(pair).type, context.Mask());
        Refused(context, "board.score", MaskType(), 1, {"a pair", "mask"});
    }

    TEST_F(ProgramDomainCheckerTest, ACallOfAnOperationIsACallDomainNodeOfTheTypeOfItsResult) {
        const DomainCheckerContext context;
        const TypedExpr count = Accepted(context, "popcount(and_mask(board.walls, shift(board.walls, East))) > 3", DescribeType<bool>());
        ASSERT_EQ(Root(count).operandCount, 2U);
        const TypedNode& call = count[Root(count).operands[0]];
        EXPECT_EQ(call.opcode, Opcode::CallDomain);
        EXPECT_EQ(call.immediate, context.toys.popcount);
        EXPECT_EQ(call.type, ExprType(ProgramType::Int32));
        const TypedNode& merged = count[call.operands[0]];
        EXPECT_EQ(merged.immediate, context.toys.bitsAnd);
        EXPECT_EQ(merged.type, context.Mask());
        EXPECT_EQ(merged.operandCount, 2U);

        const TypedExpr three = Accepted(context, "majority(board.walls, board.walls, shift(board.walls, West))", MaskType());
        EXPECT_EQ(Root(three).immediate, context.toys.majority);
        EXPECT_EQ(Root(three).operandCount, 3U);
        const TypedExpr none = Accepted(context, "popcount(walls())", DescribeType<int32_t>());
        EXPECT_EQ(none[Root(none).operands[0]].operandCount, 0U);
    }

    TEST_F(ProgramDomainCheckerTest, EqualityAndSelectOfARegisteredTypeUseTheRowsOfTheDomainType) {
        const DomainCheckerContext context;
        const TypedExpr equal = Accepted(context, "and_mask(board.walls, board.walls) == board.walls", DescribeType<bool>());
        EXPECT_EQ(Root(equal).opcode, OpcodeOf(ProgramOperation::Eq, ProgramType::Domain));
        const TypedExpr different = Accepted(context, "board.walls != or_mask(board.walls, board.walls)", DescribeType<bool>());
        EXPECT_EQ(Root(different).opcode, OpcodeOf(ProgramOperation::Ne, ProgramType::Domain));
        const TypedExpr chosen = Accepted(context, "select(board.turn > 1, board.walls, shift(board.walls, North))", MaskType());
        EXPECT_EQ(Root(chosen).opcode, OpcodeOf(ProgramOperation::Select, ProgramType::Domain));
        EXPECT_EQ(Root(chosen).type, context.Mask());
    }

    TEST_F(ProgramDomainCheckerTest, AParameterTypeIsTheHintOfItsArgument) {
        const DomainCheckerContext context;
        // `pair_of` takes two int64: a literal beside no name takes the type of its parameter
        const TypedExpr pair = Accepted(context, "pair_of(1, 2)", DescribeType<DomainPair>());
        const TypedNode& first = pair[Root(pair).operands[0]];
        EXPECT_EQ(first.type, ExprType(ProgramType::Int64));
        EXPECT_EQ(ConstantOf<int64_t>(first), 1);
        EXPECT_EQ(pair[Root(pair).operands[1]].type, ExprType(ProgramType::Int64));
        // A bare name is the enumerator of the enum the parameter has
        const TypedExpr shifted = Accepted(context, "shift(board.walls, South)", MaskType());
        const TypedNode& direction = shifted[Root(shifted).operands[1]];
        EXPECT_EQ(direction.kind, TypedNodeKind::Constant);
        EXPECT_EQ(direction.type.program, ProgramType::Enum);
        EXPECT_EQ(ConstantOf<uint32_t>(direction), static_cast<uint32_t>(DomainDirection::South));
        Refused(context, "shift(board.walls, Up)", MaskType(), 20, {"Up", "enumerator"});
    }

    TEST_F(ProgramDomainCheckerTest, TheArgumentTypesPickAmongTheOverloadsOfOneCount) {
        const DomainCheckerContext context;
        const TypedExpr withInt = Accepted(context, "chk_merge(board.walls, 3)", DescribeType<int32_t>());
        EXPECT_EQ(Root(withInt).immediate, context.more.mergeWithInt);
        const TypedExpr withMask = Accepted(context, "chk_merge(board.walls, board.walls)", MaskType());
        EXPECT_EQ(Root(withMask).immediate, context.more.mergeWithMask);
        // The overloads disagree on the second parameter, so a name there has no hint and an enumerator cannot be told
        Refused(context, "chk_merge(board.walls, North)", MaskType(), 24, {"North"});
        // The count picks: one name with one and two parameters
        EXPECT_NE(Root(Accepted(context, "chk_count(board.walls)", DescribeType<int32_t>())).immediate, Root(Accepted(context, "chk_count(board.walls, 1)", DescribeType<int32_t>())).immediate);
    }

    TEST_F(ProgramDomainCheckerTest, AnUnknownOperationListsTheFunctionsAndTheRegisteredOperations) {
        const DomainCheckerContext context;
        Refused(context, "bits(board.walls)", DescribeType<int32_t>(), 1, {"unknown function 'bits'", "popcount", "shift", "lerp", "select"});
        Refused(context, "popcount", DescribeType<int32_t>(), 1, {"'popcount' is a function"});
    }

    TEST_F(ProgramDomainCheckerTest, AWrongCountOrArgumentTypeNamesTheCallTheTypesFoundAndEverySignature) {
        const DomainCheckerContext context;
        Refused(context, "popcount(board.walls, board.walls)", DescribeType<int32_t>(), 1, {"'popcount' takes 1 argument", "2 are given", "popcount(mask) -> int"});
        Refused(context, "chk_count()", DescribeType<int32_t>(), 1, {"'chk_count' takes 1 or 2 arguments", "0 are given"});
        Refused(context, "1 + popcount(board.turn)", DescribeType<int32_t>(), 5, {"no overload of 'popcount' takes (int)", "popcount(mask) -> int"});
        Refused(context, "chk_merge(board.turn, 3)", DescribeType<int32_t>(), 1, {"no overload of 'chk_merge' takes (int, int)", "chk_merge(mask, int) -> int", "chk_merge(mask, mask) -> mask"});
    }

    TEST_F(ProgramDomainCheckerTest, ARegisteredTypeHasNoArithmeticOrOrderingAndTheMessageSaysWhatItHas) {
        const DomainCheckerContext context;
        Refused(context, "board.walls + board.walls", MaskType(), 13, {"'+' is not defined for mask", "has:", "and_mask", "popcount", "=="});
        Refused(context, "board.walls < board.walls", DescribeType<bool>(), 13, {"'<' is not defined for mask"});
        Refused(context, "-board.walls", MaskType(), 1, {"'-' is not defined for mask"});
        Refused(context, "min(board.walls, board.walls)", MaskType(), 1, {"'min' is not defined for mask"});
        Refused(context, "board.walls.x", DescribeType<int32_t>(), 1, {"no components", "mask"});
        Refused(context, "float(board.walls)", DescribeType<float>(), 7, {"converts a number", "mask"});
        Refused(context, "board.walls + 1", MaskType(), 13, {"mask"});
    }

    TEST_F(ProgramDomainCheckerTest, ATypeWithoutBitwiseEqualityHasNoEqualityOrSelect) {
        const DomainCheckerContext context;
        ASSERT_EQ(ProgramDomain::Type(context.toys.weighted).equality, DomainEquality::None);
        Refused(context, "chk_weighted(1) == chk_weighted(2)", DescribeType<bool>(), 17, {"'==' is not defined for weighted", "weight_count"});
        Refused(context, "chk_weighted(1) != chk_weighted(2)", DescribeType<bool>(), 17, {"'!=' is not defined for weighted"});
        Refused(context, "select(true, chk_weighted(1), chk_weighted(2))", DescribeType<DomainWeighted>(), 1, {"'select' is not defined for weighted"});
        // What it has is what takes it
        Accepted(context, "weight_count(chk_weighted(4))", DescribeType<int32_t>());
    }

    TEST_F(ProgramDomainCheckerTest, AResultOfTheWrongTypeNamesBothTypes) {
        const DomainCheckerContext context;
        Refused(context, "and_mask(board.walls, board.walls)", DescribeType<int32_t>(), 1, {"a mask", "int"});
        Refused(context, "board.turn", MaskType(), 1, {"an int", "mask"});
        Refused(context, "popcount(board.walls)", MaskType(), 1, {"an int", "mask"});
    }

    TEST_F(ProgramDomainCheckerTest, ATypeNameIsNoFunctionAndNoValue) {
        const DomainCheckerContext context;
        Refused(context, "mask(board.walls)", MaskType(), 1, {"'mask' is a type, not a function"});
        Refused(context, "popcount(mask)", DescribeType<int32_t>(), 10, {"'mask' is a type, not a value"});
    }

    TEST_F(ProgramDomainCheckerTest, ATypeNobodyRegisteredIsNoTypeOfAFormulaAndTheMessageListsTheRegisteredOnes) {
        const DomainCheckerContext context;
        const auto expected = CheckExpectedType(DescribeType<DomainUnregistered>());
        ASSERT_FALSE(expected.has_value());
        EXPECT_TRUE(Contains(expected.error().message, "mask")) << expected.error().message.c_str();
        EXPECT_TRUE(Contains(expected.error().message, "pair")) << expected.error().message.c_str();
        EXPECT_TRUE(Contains(expected.error().message, "vec3")) << expected.error().message.c_str();
        const auto registered = CheckExpectedType(MaskType());
        ASSERT_TRUE(registered.has_value());
        EXPECT_EQ(*registered, context.Mask());
    }

}  // namespace BECore::Tests
