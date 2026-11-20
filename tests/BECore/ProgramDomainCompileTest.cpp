// The compiler over a game's registered types and operations (Values/Programs/Domain/CLAUDE.md): formulas over the toy mask of `ProgramDomainTypes.h` compile, fold and run
// as the same code in C++ does. The document form of these values is ProgramDomainLoadTest.cpp.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Programs/ExprChecker.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;

    namespace {

        [[nodiscard]] DomainBits256 PatternMask(uint64_t seed) {
            DomainBits256 mask;
            for (size_t i = 0; i < mask.words.size(); ++i) {
                mask.words[i] = (seed + i) * 0x9E3779B97F4A7C15ULL;
            }
            return mask;
        }

        /// A context with a `DomainBoard` at level 0, named `board`
        struct CompileWorld {
            ContextDecl declaration;
            DomainBoard board;
            Context context;
            const DomainToys& toys = RegisterDomainToys();

            CompileWorld() {
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                context = Context(declaration);
                context.Set(0, board);
            }

            CompileWorld(const CompileWorld&) = delete;
            CompileWorld& operator=(const CompileWorld&) = delete;
        };

        /// A context function that declares the level it reads, registered once per process
        [[nodiscard]] DomainBits256 WatchedWalls(const Context& context) {
            return BoardWalls(context);
        }

        [[nodiscard]] DomainOperationId RegisterWatchedWalls() {
            static const DomainOperationId id = [] {
                static_cast<void>(RegisterDomainToys());
                return Require(ProgramDomain::AddOperation<&WatchedWalls>("walls_watched", DomainOperationOptions{LevelBit(0)}));
            }();
            return id;
        }

        template <typename T>
        struct Formula {
            StringView text;
            T (*reference)(const DomainBoard&);
        };

        /// The formula compiled and run over several boards equals the C++ reference, and it is a program (not a constant or a slot)
        template <typename T, typename Equal>
        void ExpectFormula(const Formula<T>& formula, Equal equal) {
            CompileWorld world;
            const auto compiled = CompileValue<T>(formula.text, world.declaration);
            ASSERT_TRUE(compiled.has_value()) << String(formula.text).c_str() << ": " << compiled.error().message.c_str();
            EXPECT_EQ(compiled->Kind(), ValueKind::Program) << String(formula.text).c_str();
            for (const uint64_t seed : {1ULL, 7ULL, 23ULL, 99ULL}) {
                world.board.walls = PatternMask(seed);
                world.board.turn = static_cast<int32_t>(seed % 5);
                EXPECT_TRUE(equal(compiled->Evaluate(world.context), formula.reference(world.board))) << String(formula.text).c_str() << " seed " << seed;
            }
        }

        [[nodiscard]] DomainBits256 WallsOfRow(const DomainBoard& board, int32_t row) {
            DomainBits256 rowMask;
            for (int32_t x = 0; x < kDomainBoardWidth; ++x) {
                rowMask = BitsOr(rowMask, BitsCell(x, row));
            }
            return BitsAnd(board.walls, rowMask);
        }

        bool SameMask(const DomainBits256& left, const DomainBits256& right) {
            return left == right;
        }

        bool SameInt(int32_t left, int32_t right) {
            return left == right;
        }

        bool SameBool(bool left, bool right) {
            return left == right;
        }

        template <typename T>
        T ImageAs(const RegisterImage& image) {
            T value{};
            std::memcpy(&value, image.bytes.data(), sizeof(T));
            return value;
        }

    }  // namespace

    TEST(ProgramDomainCompileTest, FormulasOverARegisteredTypeRunAsTheSameCodeInCpp) {
        ExpectFormula<int32_t>(
            {"popcount(and_mask(board.walls, shift(board.walls, East)))", [](const DomainBoard& b) { return BitsPopcount(BitsAnd(b.walls, BitsShift(b.walls, DomainDirection::East))); }}, SameInt);
        ExpectFormula<bool>(
            {"popcount(and_mask(board.walls, shift(board.walls, East))) > 3", [](const DomainBoard& b) { return BitsPopcount(BitsAnd(b.walls, BitsShift(b.walls, DomainDirection::East))) > 3; }},
            SameBool);
        ExpectFormula<DomainBits256>(
            {"select(board.turn > 1, shift(board.walls, North), board.walls)", [](const DomainBoard& b) { return b.turn > 1 ? BitsShift(b.walls, DomainDirection::North) : b.walls; }}, SameMask);
        ExpectFormula<DomainBits256>({"majority(board.walls, shift(board.walls, East), shift(board.walls, South))",
                                      [](const DomainBoard& b) { return BitsMajority(b.walls, BitsShift(b.walls, DomainDirection::East), BitsShift(b.walls, DomainDirection::South)); }},
                                     SameMask);
        ExpectFormula<bool>({"any(and_mask(board.walls, bit(board.turn)))", [](const DomainBoard& b) { return BitsAny(BitsAnd(b.walls, BitsBit(b.turn))); }}, SameBool);
        ExpectFormula<bool>({"or_mask(board.walls, cell(3, 4)) != board.walls", [](const DomainBoard& b) { return BitsOr(b.walls, BitsCell(3, 4)) != b.walls; }}, SameBool);
    }

    TEST(ProgramDomainCompileTest, AnOperationThatReadsTheContextRunsOverTheBoardEvenWithConstantArguments) {
        // `walls_of_row(2)` has only constant arguments but reads the board: folding it would run it over an empty context
        ExpectFormula<int32_t>({"popcount(walls_of_row(2))", [](const DomainBoard& b) { return BitsPopcount(WallsOfRow(b, 2)); }}, SameInt);
        ExpectFormula<DomainBits256>({"walls()", [](const DomainBoard& b) { return b.walls; }}, SameMask);
    }

    TEST(ProgramDomainCompileTest, AConstantCallFoldsThroughTheInterpreterToAnImageOfTheType) {
        CompileWorld world;
        const auto cell = CompileExpression("cell(3, 4)", world.declaration, DescribeType<DomainBits256>());
        ASSERT_TRUE(cell.has_value()) << cell.error().message.c_str();
        EXPECT_EQ(cell->kind, CompiledKind::Constant);
        EXPECT_EQ(cell->type, ProgramType::Domain);
        EXPECT_EQ(cell->domain, world.toys.mask);
        EXPECT_EQ(ImageAs<DomainBits256>(cell->constant), BitsCell(3, 4));

        const auto value = CompileValue<DomainBits256>("or_mask(cell(3, 4), shift(cell(3, 4), East))", world.declaration);
        ASSERT_TRUE(value.has_value()) << value.error().message.c_str();
        ASSERT_TRUE(value->IsConst());
        EXPECT_EQ(value->GetConst(), BitsOr(BitsCell(3, 4), BitsShift(BitsCell(3, 4), DomainDirection::East)));

        // A constant of a smaller registered type keeps the bytes of the type only
        const auto pair = CompileValue<DomainPair>("pair_of(3, 4)", world.declaration);
        ASSERT_TRUE(pair.has_value()) << pair.error().message.c_str();
        ASSERT_TRUE(pair->IsConst());
        EXPECT_EQ(pair->GetConst(), (DomainPair{3, 4}));

        // The result of an operation of a registered type is a constant of an engine type as well
        const auto count = CompileValue<int32_t>("popcount(or_mask(cell(3, 4), cell(5, 6)))", world.declaration);
        ASSERT_TRUE(count.has_value());
        ASSERT_TRUE(count->IsConst());
        EXPECT_EQ(count->GetConst(), 2);
    }

    TEST(ProgramDomainCompileTest, AnOperationIsTotalSoAnArgumentOutOfRangeFoldsToItsDocumentedResult) {
        CompileWorld world;
        const auto outside = CompileValue<DomainBits256>("cell(99, 99)", world.declaration);
        ASSERT_TRUE(outside.has_value());
        ASSERT_TRUE(outside->IsConst());
        EXPECT_EQ(outside->GetConst(), DomainBits256{});
        const auto negative = CompileValue<DomainBits256>("or_mask(bit(-1), cell(-3, 2))", world.declaration);
        ASSERT_TRUE(negative.has_value());
        ASSERT_TRUE(negative->IsConst());
        EXPECT_EQ(negative->GetConst(), DomainBits256{});
    }

    TEST(ProgramDomainCompileTest, TheResultCarriesTheRegisteredTypeOfAProgramAndOfASlot) {
        CompileWorld world;
        const auto program = CompileExpression("and_mask(board.walls, board.walls)", world.declaration, DescribeType<DomainBits256>());
        ASSERT_TRUE(program.has_value()) << program.error().message.c_str();
        EXPECT_EQ(program->kind, CompiledKind::Program);
        EXPECT_EQ(program->domain, world.toys.mask);

        const auto slot = CompileExpression("board.score", world.declaration, DescribeType<DomainPair>());
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(slot->kind, CompiledKind::Slot);
        EXPECT_EQ(slot->domain, world.toys.pair);
        EXPECT_EQ(slot->path, "board.score");

        const auto number = CompileExpression("board.turn", world.declaration, DescribeType<int32_t>());
        ASSERT_TRUE(number.has_value());
        EXPECT_EQ(number->domain, kNoDomainType);
    }

    TEST(ProgramDomainCompileTest, AValueOfARegisteredTypeSmallerThanTheRegisterIsReadWholeAndComputedWhole) {
        CompileWorld world;
        world.board.score = DomainPair{-5, 77};
        world.board.turn = 9;
        const auto slot = CompileValue<DomainPair>("board.score", world.declaration);
        ASSERT_TRUE(slot.has_value());
        ASSERT_EQ(slot->Kind(), ValueKind::Slot);
        EXPECT_EQ(slot->Evaluate(world.context), world.board.score);

        const auto program = CompileValue<DomainPair>("select(board.turn > 3, board.score, pair_of(int64(board.turn), 7))", world.declaration);
        ASSERT_TRUE(program.has_value()) << program.error().message.c_str();
        ASSERT_EQ(program->Kind(), ValueKind::Program);
        EXPECT_EQ(program->Evaluate(world.context), world.board.score);
        world.board.turn = 2;
        EXPECT_EQ(program->Evaluate(world.context), (DomainPair{2, 7}));
    }

    TEST(ProgramDomainCompileTest, TheSameCodeIsOneProgramHoweverItIsWrittenAndAnotherOperationIsAnotherProgram) {
        CompileWorld world;
        const auto first = CompileValue<int32_t>("popcount(and_mask(board.walls, shift(board.walls, East)))", world.declaration);
        const auto spaced = CompileValue<int32_t>("popcount( and_mask(board.walls,shift( board.walls , East )) )", world.declaration);
        const auto other = CompileValue<int32_t>("popcount(or_mask(board.walls, shift(board.walls, East)))", world.declaration);
        ASSERT_TRUE(first.has_value() && spaced.has_value() && other.has_value());
        EXPECT_EQ(first->Id(), spaced->Id());
        EXPECT_NE(first->Id(), other->Id());
    }

    TEST(ProgramDomainCompileTest, AnOperationThatDeclaresLevelsPutsThemInTheReadsOfTheProgram) {
        CompileWorld world;
        static_cast<void>(RegisterWatchedWalls());
        const auto program = CompileValue<int32_t>("popcount(walls_watched())", world.declaration);
        ASSERT_TRUE(program.has_value()) << program.error().message.c_str();
        ASSERT_EQ(program->Kind(), ValueKind::Program);
        // The text names no path, so the level is only in what the operation declared
        EXPECT_EQ(program->Reads(), LevelBit(0));
        const auto undeclared = CompileValue<int32_t>("popcount(walls())", world.declaration);
        ASSERT_TRUE(undeclared.has_value());
        EXPECT_EQ(undeclared->Reads(), LevelMask{0});
    }

    TEST(ProgramDomainCompileTest, TheRegisterLimitIsStillThirtyTwoWithThreeArgumentOperations) {
        CompileWorld world;
        // k nested calls whose last argument is the next call need 2k + 1 registers: the first operand and the second stay while the third nests
        const auto nested = [](size_t depth) {
            String text = "board.walls";
            for (size_t i = 0; i < depth; ++i) {
                text = Format("majority(board.walls, shift(board.walls, East), {})", text);
            }
            return text;
        };
        const auto reference = [&](size_t depth, const DomainBits256& walls) {
            DomainBits256 result = walls;
            for (size_t i = 0; i < depth; ++i) {
                result = BitsMajority(walls, BitsShift(walls, DomainDirection::East), result);
            }
            return result;
        };
        constexpr size_t kFits = (kMaxProgramRegisters - 1) / 2;
        const auto fitting = CompileValue<DomainBits256>(nested(kFits), world.declaration);
        ASSERT_TRUE(fitting.has_value()) << fitting.error().message.c_str();
        world.board.walls = PatternMask(13);
        EXPECT_EQ(fitting->Evaluate(world.context), reference(kFits, world.board.walls));

        const size_t count = ProgramPool::Count();
        const auto refused = CompileValue<DomainBits256>(nested(kFits + 1), world.declaration);
        ASSERT_FALSE(refused.has_value());
        EXPECT_TRUE(Contains(refused.error().message, "32 registers")) << refused.error().message.c_str();
        EXPECT_GT(refused.error().column, 0U);
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(ProgramDomainCompileTest, ATypeNobodyRegisteredIsStillTheErrorThatListsTheTypes) {
        CompileWorld world;
        const auto refused = CompileValue<DomainUnregistered>("walls()", world.declaration);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error().column, 0U);
        // The registered types follow the engine's, whatever other cases registered in this process
        EXPECT_TRUE(Contains(refused.error().message, "vec2, vec3, vec4, ivec2, quat, color, enum")) << refused.error().message.c_str();
        EXPECT_TRUE(Contains(refused.error().message, "mask")) << refused.error().message.c_str();
        EXPECT_TRUE(Contains(refused.error().message, "DomainUnregistered")) << refused.error().message.c_str();
    }

}  // namespace BECore::Tests
