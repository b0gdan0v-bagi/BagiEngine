// What the program pool holds of a program (Values/Programs/Program.h): the code and constants of an interned program are in the pool's arena, a hand-assembled one owns its vectors.

#include <BECore/Memory/ArenaStats.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Require.h>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr const char* kPoolArenaName = "ProgramPool";
        constexpr int32_t kSaltStride = 100000;
        constexpr uint32_t kTableEntries = 5;
        constexpr uint32_t kThreads = 2;
        constexpr uint32_t kProgramsPerThread = 40;

        /// Each case takes a range of constants of its own, so the programs it interns are new to the pool
        int32_t NextBase() {
            static int32_t salt = 0;
            return ++salt * kSaltStride;
        }

        Program ConstProgram(int32_t value) {
            ProgramBuilder builder;
            const uint8_t dst = Require(builder.AcquireRegister());
            builder.EmitLoadConst(dst, value);
            return Require(builder.Finish(kProgramTypeOf<int32_t>, dst));
        }

        /// No constant at all: the loaded slot is told apart by its offset
        Program SlotProgram(uint32_t offset) {
            ProgramBuilder builder;
            const uint8_t dst = Require(builder.AcquireRegister());
            builder.EmitLoadSlot(dst, ProgramType::Int32, SlotRef{0, offset});
            return Require(builder.Finish(kProgramTypeOf<int32_t>, dst));
        }

        /// The entry at @p index of a table of @p first, @p first + 1, ...: kTableEntries registers of constants after the index
        Program TableProgram(int32_t first, int32_t index) {
            ProgramBuilder builder;
            const uint8_t indexReg = Require(builder.AcquireRegister());
            builder.EmitLoadConst(indexReg, index);
            Vector<RegisterImage> images;
            for (uint32_t i = 0; i < kTableEntries; ++i) {
                const int32_t entry = first + static_cast<int32_t>(i);
                RegisterImage image{};
                std::memcpy(image.bytes.data(), &entry, sizeof(entry));
                images.push_back(image);
            }
            const uint32_t run = builder.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
            const uint8_t dst = Require(builder.AcquireRegister());
            builder.EmitLoadTable(dst, indexReg, run, kTableEntries);
            return Require(builder.Finish(kProgramTypeOf<int32_t>, dst));
        }

        ProgramId InternOrFail(Program program) {
            const Optional<ProgramId> id = ProgramPool::Intern(std::move(program));
            EXPECT_TRUE(id.has_value());
            return id.value_or(0);
        }

        uint64_t PoolArenaUsed() {
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (std::strcmp(entry.name, kPoolArenaName) == 0) {
                    return entry.used;
                }
            }
            return 0;
        }

    }  // namespace

    TEST(ProgramPoolTest, AHandAssembledProgramOwnsItsCode) {
        const Program program = ConstProgram(NextBase());
        EXPECT_TRUE(program.Owns());
        EXPECT_FALSE(program.Code().empty());
        EXPECT_FALSE(program.Constants().empty());
    }

    TEST(ProgramPoolTest, AnInternedProgramViewsTheArenaAndReadsTheSame) {
        const Program program = ConstProgram(NextBase());
        const ProgramId id = InternOrFail(program);
        const Program& pooled = ProgramPool::Get(id);
        EXPECT_FALSE(pooled.Owns());
        EXPECT_TRUE(pooled.SameCode(program));
        EXPECT_EQ(pooled.Disassemble(), program.Disassemble());
        EXPECT_EQ(pooled.CodeHash(), program.CodeHash());
        EXPECT_NE(pooled.Code().data(), program.Code().data());
        EXPECT_NE(pooled.Constants().data(), program.Constants().data());
        EXPECT_EQ(Run<int32_t>(pooled, Context{}), Run<int32_t>(program, Context{}));
    }

    TEST(ProgramPoolTest, ACopyOfAnOwningProgramOwnsItsOwnBytes) {
        const Program program = ConstProgram(NextBase());
        const Program copy = program;
        EXPECT_TRUE(copy.Owns());
        EXPECT_TRUE(copy.SameCode(program));
        EXPECT_NE(copy.Code().data(), program.Code().data());
        EXPECT_NE(copy.Constants().data(), program.Constants().data());

        Program assigned = ConstProgram(NextBase());
        assigned = program;
        EXPECT_TRUE(assigned.Owns());
        EXPECT_TRUE(assigned.SameCode(program));
        EXPECT_NE(assigned.Code().data(), program.Code().data());
    }

    TEST(ProgramPoolTest, AMovedFromOwningProgramIsEmpty) {
        const int32_t value = NextBase();
        Program source = ConstProgram(value);
        const Program expected = source;
        const Program moved = std::move(source);
        EXPECT_TRUE(moved.SameCode(expected));
        EXPECT_EQ(Run<int32_t>(moved, Context{}), value);
        EXPECT_TRUE(source.Code().empty());       // NOLINT: the moved-from state is the contract under test
        EXPECT_TRUE(source.Constants().empty());  // NOLINT

        Program target;
        Program again = expected;
        target = std::move(again);
        EXPECT_TRUE(target.SameCode(expected));
        EXPECT_TRUE(again.Code().empty());  // NOLINT
    }

    TEST(ProgramPoolTest, ACopyOfAPooledProgramSharesTheArenaAndOwnsNothing) {
        const Program& pooled = ProgramPool::Get(InternOrFail(ConstProgram(NextBase())));
        const Program copy = pooled;
        EXPECT_FALSE(copy.Owns());
        EXPECT_EQ(copy.Code().data(), pooled.Code().data());
        EXPECT_EQ(copy.Constants().data(), pooled.Constants().data());
        EXPECT_TRUE(copy.SameCode(pooled));

        Program moved = copy;
        const Program target = std::move(moved);
        EXPECT_FALSE(target.Owns());
        EXPECT_EQ(target.Code().data(), pooled.Code().data());
    }

    TEST(ProgramPoolTest, ASecondInternOfEqualCodeAddsNothingToTheArena) {
        const int32_t value = NextBase();
        const ProgramId first = InternOrFail(ConstProgram(value));
        const uint64_t used = PoolArenaUsed();
        const size_t count = ProgramPool::Count();
        EXPECT_GT(used, 0U);
        EXPECT_EQ(InternOrFail(ConstProgram(value)), first);
        EXPECT_EQ(PoolArenaUsed(), used);
        EXPECT_EQ(ProgramPool::Count(), count);
    }

    TEST(ProgramPoolTest, ProgramsWithoutConstantsAndWithManyReadBackRight) {
        const int32_t base = NextBase();
        const Program bare = SlotProgram(static_cast<uint32_t>(base));
        ASSERT_TRUE(bare.Constants().empty());
        const Program& pooledBare = ProgramPool::Get(InternOrFail(bare));
        EXPECT_TRUE(pooledBare.Constants().empty());
        EXPECT_TRUE(pooledBare.SameCode(bare));

        for (int32_t index = 0; index < static_cast<int32_t>(kTableEntries); ++index) {
            const Program table = TableProgram(base, index);
            ASSERT_GT(table.Constants().size(), 1U);
            const Program& pooled = ProgramPool::Get(InternOrFail(table));
            EXPECT_TRUE(pooled.SameCode(table));
            EXPECT_EQ(Run<int32_t>(pooled, Context{}), base + index);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(pooled.Constants().data()) % alignof(RegisterImage), 0U);
        }
    }

    TEST(ProgramPoolTest, TwoThreadsInterningGetBackWhatTheyInterned) {
        const int32_t base = NextBase();
        Vector<Vector<Program>> made(kThreads);
        Vector<Vector<Optional<ProgramId>>> ids(kThreads);
        for (uint32_t t = 0; t < kThreads; ++t) {
            for (uint32_t i = 0; i < kProgramsPerThread; ++i) {
                made[t].push_back(ConstProgram(base + static_cast<int32_t>(t * kProgramsPerThread + i)));
            }
            ids[t].resize(kProgramsPerThread);
        }
        Vector<std::thread> threads;
        for (uint32_t t = 0; t < kThreads; ++t) {
            threads.emplace_back([&made, &ids, t] {
                for (uint32_t i = 0; i < kProgramsPerThread; ++i) {
                    ids[t][i] = ProgramPool::Intern(made[t][i]);
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        for (uint32_t t = 0; t < kThreads; ++t) {
            for (uint32_t i = 0; i < kProgramsPerThread; ++i) {
                ASSERT_TRUE(ids[t][i].has_value());
                const Program& pooled = ProgramPool::Get(*ids[t][i]);
                EXPECT_FALSE(pooled.Owns());
                EXPECT_TRUE(pooled.SameCode(made[t][i]));
                EXPECT_EQ(Run<int32_t>(pooled, Context{}), base + static_cast<int32_t>(t * kProgramsPerThread + i));
            }
        }
    }

}  // namespace BECore::Tests
