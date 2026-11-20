// Hostile and extreme input over a game's registered types and operations: a formula that is too deep, too wide or wrongly spelled is an error and adds nothing to the pool; an
// operation is total; a registration after content loaded changes no program already in the pool. The toy mask and its operations are those of `ProgramDomainTypes.h`.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ProgramDomainData;

    namespace {

        struct HostileWorld {
            ContextDecl declaration;
            DomainBoard board;
            Context context;
            const DomainToys& toys = RegisterDomainToys();

            HostileWorld() {
                EXPECT_TRUE(declaration.Add<DomainBoard>("board").has_value());
                context = Context(declaration);
                context.Set(0, board);
            }

            HostileWorld(const HostileWorld&) = delete;
            HostileWorld& operator=(const HostileWorld&) = delete;
        };

        /// `and_mask(and_mask(... board.walls ..., board.walls), board.walls)`: every level is one more call over the one before
        String LeftChain(size_t calls) {
            String text = "board.walls";
            for (size_t i = 0; i < calls; ++i) {
                text = Format("and_mask({}, board.walls)", text);
            }
            return text;
        }

        String Repeated(StringView part, size_t count) {
            String text;
            for (size_t i = 0; i < count; ++i) {
                text.append(part.data(), part.size());
            }
            return text;
        }

        template <typename T>
        void ExpectRefusedAndPoolUnchanged(const HostileWorld& world, StringView text, StringView words) {
            const size_t pooled = ProgramPool::Count();
            const auto compiled = CompileValue<T>(text, world.declaration);
            ASSERT_FALSE(compiled.has_value()) << String(text.substr(0, 80)).c_str();
            EXPECT_TRUE(Contains(compiled.error().message, words)) << String(text.substr(0, 80)).c_str() << ": " << compiled.error().message.c_str();
            EXPECT_EQ(ProgramPool::Count(), pooled) << String(text.substr(0, 80)).c_str();
        }

        [[nodiscard]] int32_t LatePopcountOfPair(const DomainPair& pair) {
            return static_cast<int32_t>(pair.first + pair.second);
        }

    }  // namespace

    TEST(ProgramDomainHostileTest, ACallChainIsRefusedAtTheHeightLimitAndNeverBeyond) {
        HostileWorld world;
        world.board.walls = DomainBits256{{0xF0F0, 0, 1, 2}};
        constexpr size_t kLevels = ExprLimits::kMaxTreeHeight;
        const auto fits = CompileValue<DomainBits256>(LeftChain(kLevels), world.declaration);
        ASSERT_TRUE(fits.has_value()) << fits.error().message.c_str();
        EXPECT_EQ(fits->Evaluate(world.context), world.board.walls);
        for (size_t calls = kLevels + 1; calls <= kLevels + 8; ++calls) {
            ExpectRefusedAndPoolUnchanged<DomainBits256>(world, LeftChain(calls), "levels high");
        }
        // The same limit holds for calls nested as an argument of another operation
        ExpectRefusedAndPoolUnchanged<int32_t>(world, Format("popcount({})", LeftChain(kLevels)), "levels high");
    }

    TEST(ProgramDomainHostileTest, NestingBeyondTheLimitIsRefusedWhateverIsInsideTheParentheses) {
        HostileWorld world;
        constexpr size_t kLevels = ExprLimits::kMaxNesting;
        const String fits = Format("{}board.walls{}", Repeated("(", kLevels), Repeated(")", kLevels));
        ASSERT_TRUE(CompileValue<DomainBits256>(fits, world.declaration).has_value());
        ExpectRefusedAndPoolUnchanged<DomainBits256>(world, Format("{}board.walls{}", Repeated("(", kLevels + 1), Repeated(")", kLevels + 1)), "nested deeper");
        ExpectRefusedAndPoolUnchanged<DomainBits256>(world, Format("{}board.walls{}", Repeated("and_mask(", kLevels + 1), Repeated(", board.walls)", kLevels + 1)), "nested deeper");
    }

    TEST(ProgramDomainHostileTest, ThreeArgumentChainsFitTheRegistersOrAreRefusedByTheLimitThatStopsThemAndNeverAddToThePool) {
        HostileWorld world;
        world.board.walls = DomainBits256{{0xFF00FF, 3, 0, 9}};
        // The last argument nests, so a chain of k calls holds 2k + 1 registers at once and is k levels high
        constexpr size_t kFitsRegisters = (kMaxProgramRegisters - 1) / 2;
        constexpr size_t kHeight = ExprLimits::kMaxTreeHeight;
        static_assert(kFitsRegisters < kHeight);
        for (size_t depth = 1; depth <= kHeight + 8; ++depth) {
            String text = "board.walls";
            for (size_t i = 0; i < depth; ++i) {
                text = Format("majority(board.walls, shift(board.walls, East), {})", text);
            }
            const size_t pooled = ProgramPool::Count();
            const auto compiled = CompileValue<DomainBits256>(text, world.declaration);
            if (depth <= kFitsRegisters) {
                ASSERT_TRUE(compiled.has_value()) << depth << ": " << compiled.error().message.c_str();
                static_cast<void>(compiled->Evaluate(world.context));
                continue;
            }
            ASSERT_FALSE(compiled.has_value()) << depth;
            // The register limit stops a chain first; the height limit stops a longer one at the parse
            EXPECT_TRUE(Contains(compiled.error().message, "32 registers") || Contains(compiled.error().message, "levels high")) << depth << ": " << compiled.error().message.c_str();
            if (depth == kFitsRegisters + 1) {
                EXPECT_TRUE(Contains(compiled.error().message, "32 registers")) << compiled.error().message.c_str();
            }
            EXPECT_GT(compiled.error().column, 0U) << depth;
            EXPECT_EQ(ProgramPool::Count(), pooled) << depth;
        }
    }

    TEST(ProgramDomainHostileTest, TheNodeAndTextLimitsApplyToCallsOfRegisteredOperations) {
        HostileWorld world;
        // 512 nodes at most, however short the text is
        const String wide = Format("popcount(majority({}1))", Repeated("1, ", ExprLimits::kMaxNodes));
        ExpectRefusedAndPoolUnchanged<int32_t>(world, wide, "more than 512 parts");
        ExpectRefusedAndPoolUnchanged<int32_t>(world, Format("popcount(and_mask(board.walls, {}))", String(ExprLimits::kMaxTextBytes, ' ') + "board.walls"), "longer than 4096 bytes");
    }

    TEST(ProgramDomainHostileTest, AMisspelledOrMisusedCallIsAnErrorAndAddsNothingToThePool) {
        HostileWorld world;
        const StringView texts[] = {"popcount(",
                                    "popcount()",
                                    "popcount(board.walls",
                                    "and_mask(,)",
                                    "and_mask(board.walls,)",
                                    "and_mask(board.walls)",
                                    "and_mask(1, 2)",
                                    "and_mask(board.walls, board.walls, board.walls)",
                                    "shift(board.walls, East, East)",
                                    "shift(board.walls, 3)",
                                    "shift(board.walls, Up)",
                                    "cell(3 4)",
                                    "cell(1.5, 2)",
                                    "cell(true, 2)",
                                    "walls(()",
                                    "walls(1)",
                                    "popcount(popcount(board.walls))",
                                    "mask",
                                    "mask(board.walls)",
                                    "pair",
                                    "popcount.board",
                                    "board.walls(1)",
                                    "and_mask(board.walls, board.score)",
                                    "majority(board.walls, board.walls)",
                                    "\x01popcount(board.walls)"};
        for (const StringView text : texts) {
            const size_t pooled = ProgramPool::Count();
            const auto compiled = CompileValue<int32_t>(text, world.declaration);
            EXPECT_FALSE(compiled.has_value()) << String(text).c_str();
            EXPECT_EQ(ProgramPool::Count(), pooled) << String(text).c_str();
            const auto asMask = CompileValue<DomainBits256>(text, world.declaration);
            EXPECT_EQ(ProgramPool::Count(), pooled) << String(text).c_str();
            if (!asMask.has_value()) {
                EXPECT_FALSE(asMask.error().message.empty()) << String(text).c_str();
            }
        }
        // A type name is neither a function nor a value, and says what it is
        ExpectRefusedAndPoolUnchanged<DomainBits256>(world, "mask", "mask");
    }

    TEST(ProgramDomainHostileTest, AnOperationIsTotalOverTheExtremesOfItsArguments) {
        HostileWorld world;
        constexpr int32_t kLow = std::numeric_limits<int32_t>::min();
        constexpr int32_t kHigh = std::numeric_limits<int32_t>::max();
        const int32_t extremes[] = {kLow, kLow + 1, -1, 0, 1, 15, 16, 255, 256, kHigh - 1, kHigh};
        for (const int32_t x : extremes) {
            const auto bit = CompileValue<DomainBits256>(Format("bit({})", x), world.declaration);
            ASSERT_TRUE(bit.has_value()) << x << ": " << bit.error().message.c_str();
            ASSERT_TRUE(bit->IsConst());
            EXPECT_EQ(bit->GetConst(), BitsBit(x)) << x;
            for (const int32_t y : extremes) {
                const String formula = Format("cell({}, {})", x, y);
                const auto cell = CompileValue<DomainBits256>(formula, world.declaration);
                ASSERT_TRUE(cell.has_value()) << formula.c_str() << ": " << cell.error().message.c_str();
                ASSERT_TRUE(cell->IsConst()) << formula.c_str();
                EXPECT_EQ(cell->GetConst(), BitsCell(x, y)) << formula.c_str();
            }
        }
        // A shift out of the board leaves the mask empty, not undefined
        const auto gone = CompileValue<int32_t>("popcount(shift(shift(shift(cell(15, 15), South), South), East))", world.declaration);
        ASSERT_TRUE(gone.has_value());
        EXPECT_EQ(gone->GetConst(), 0);
    }

    TEST(ProgramDomainHostileTest, AnOperationRegisteredAfterAProgramIsInThePoolChangesNothingOfThatProgram) {
        HostileWorld world;
        world.board.walls = DomainBits256{{0xAA, 0xBB, 0, 1}};
        static const DomainOperationId first = Require(ProgramDomain::AddOperation<&BitsPopcount>("late_pick"));
        ASSERT_EQ(ProgramDomain::Operation(first).parameterCount, 1U);
        const String text = "late_pick(and_mask(board.walls, shift(board.walls, East)))";
        const auto before = CompileValue<int32_t>(text, world.declaration);
        ASSERT_TRUE(before.has_value()) << before.error().message.c_str();
        ASSERT_EQ(before->Kind(), ValueKind::Program);
        const int32_t answer = before->Evaluate(world.context);
        const size_t pooled = ProgramPool::Count();
        const uint64_t fingerprint = ProgramDomain::Fingerprint();

        // The name gets a second overload over another type, once per process: a formula that fits the first still means the first
        static const bool changed = [&] {
            static_cast<void>(Require(ProgramDomain::AddOperation<&LatePopcountOfPair>("late_pick")));
            return ProgramDomain::Fingerprint() != fingerprint;
        }();
        EXPECT_TRUE(changed);
        EXPECT_EQ(ProgramPool::Count(), pooled);
        EXPECT_EQ(before->Evaluate(world.context), answer);
        const auto again = CompileValue<int32_t>(text, world.declaration);
        ASSERT_TRUE(again.has_value());
        EXPECT_EQ(again->Id(), before->Id());

        world.board.score = DomainPair{4, 5};
        const auto pair = CompileValue<int32_t>("late_pick(board.score)", world.declaration);
        ASSERT_TRUE(pair.has_value()) << pair.error().message.c_str();
        EXPECT_EQ(pair->Evaluate(world.context), 9);
    }

}  // namespace BECore::Tests
