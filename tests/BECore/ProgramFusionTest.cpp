// The peephole pass (Values/Programs/Peephole.h): each fusion computes what the unfused code computes, bit for bit, and is left alone where its rules say so.

#include "ProgramFusionTest.h"

#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Peephole.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <Generated/ProgramFusionTest.gen.hpp>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    using namespace ProgramFusionData;

    namespace {

        constexpr size_t kInputs = 3000;
        constexpr uint64_t kSeed = 0x5EEDF00DCAFEULL;
        constexpr uint64_t kSplitMixIncrement = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t kSplitMixFirst = 0xBF58476D1CE4E5B9ULL;
        constexpr uint64_t kSplitMixSecond = 0x94D049BB133111EBULL;
        constexpr uint32_t kSplitMixShiftFirst = 30;
        constexpr uint32_t kSplitMixShiftSecond = 27;
        constexpr uint32_t kSplitMixShiftLast = 31;
        constexpr uint64_t kEdgeOneIn = 2;

        /// A generator with a fixed seed: the same inputs on every machine
        class Generator {
        public:
            uint64_t Next() {
                _state += kSplitMixIncrement;
                uint64_t z = _state;
                z = (z ^ (z >> kSplitMixShiftFirst)) * kSplitMixFirst;
                z = (z ^ (z >> kSplitMixShiftSecond)) * kSplitMixSecond;
                return z ^ (z >> kSplitMixShiftLast);
            }

            bool PickEdge() {
                return Next() % kEdgeOneIn == 0;
            }

        private:
            uint64_t _state = kSeed;
        };

        template <typename T>
        T Edge(Generator& generator);

        template <>
        int32_t Edge<int32_t>(Generator& generator) {
            constexpr int32_t kEdges[] = {0, 1, -1, 2, -2, 7, 255, 256, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min() + 1, 65536};
            return kEdges[generator.Next() % std::size(kEdges)];
        }

        template <>
        int64_t Edge<int64_t>(Generator& generator) {
            constexpr int64_t kEdges[] = {0, 1, -1, 2, -2, 7, 4294967296, std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min() + 1};
            return kEdges[generator.Next() % std::size(kEdges)];
        }

        /// The values a float computation is most likely to treat specially: both zeros, the extremes, the subnormals, the infinities, NaN, the steps around 1
        template <typename F>
        F FloatEdge(Generator& generator) {
            using Limits = std::numeric_limits<F>;
            const F kEdges[] = {F{0},
                                -F{0},
                                F{1},
                                -F{1},
                                F{0.5},
                                F{2},
                                F{0.1},
                                Limits::denorm_min(),
                                -Limits::denorm_min(),
                                Limits::min(),
                                Limits::max(),
                                Limits::lowest(),
                                Limits::infinity(),
                                -Limits::infinity(),
                                Limits::quiet_NaN(),
                                F{1} + Limits::epsilon(),
                                F{1} - Limits::epsilon() / 2};
            return kEdges[generator.Next() % std::size(kEdges)];
        }

        template <>
        float Edge<float>(Generator& generator) {
            return FloatEdge<float>(generator);
        }

        template <>
        double Edge<double>(Generator& generator) {
            return FloatEdge<double>(generator);
        }

        /// An edge value or a random bit pattern of the type (every integer, every float including the NaNs with their payloads)
        template <typename T>
        T Generate(Generator& generator) {
            if (generator.PickEdge()) {
                return Edge<T>(generator);
            }
            if constexpr (sizeof(T) == sizeof(uint32_t)) {
                return std::bit_cast<T>(static_cast<uint32_t>(generator.Next()));
            } else {
                return std::bit_cast<T>(generator.Next());
            }
        }

        void Randomize(FusionStats& stats, Generator& generator) {
            stats.i32 = Generate<int32_t>(generator);
            stats.i32b = Generate<int32_t>(generator);
            stats.i64 = Generate<int64_t>(generator);
            stats.i64b = Generate<int64_t>(generator);
            stats.f32 = Generate<float>(generator);
            stats.f32b = Generate<float>(generator);
            stats.f64 = Generate<double>(generator);
            stats.f64b = Generate<double>(generator);
        }

        /// Every instruction `Fuse` writes
        bool IsFused(Opcode op) {
            return IsMulAddSlot(op);
        }

        size_t CountOf(const Program& program, Opcode op) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [op](const Instruction& ins) { return ins.op == op; }));
        }

        size_t CountFused(const Program& program) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [](const Instruction& ins) { return IsFused(ins.op); }));
        }

        const Program* Compile(const FusionWorld& world, StringView text, ProgramType type, bool fuse, const TypeDesc& wanted) {
            CompileOptions options;
            options.fuse = fuse;
            const auto compiled = CompileExpression(text, world.declaration, wanted, options);
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? String{} : compiled.error().message).c_str();
            if (!compiled.has_value()) {
                return nullptr;
            }
            EXPECT_EQ(compiled->kind, CompiledKind::Program) << String(text).c_str();
            EXPECT_EQ(compiled->type, type);
            return compiled->kind == CompiledKind::Program ? &ProgramPool::Get(compiled->program) : nullptr;
        }

        /// Compiles @p text with and without fusion, checks that the fused program has @p expected once and the plain one no fused instruction, then runs both over generated inputs
        /// and compares the bytes of the results
        template <typename T>
        void ExpectSameBits(StringView text, Opcode expected, size_t iterations = kInputs) {
            FusionWorld world;
            const Program* fused = Compile(world, text, kProgramTypeOf<T>, true, DescribeType<T>());
            const Program* plain = Compile(world, text, kProgramTypeOf<T>, false, DescribeType<T>());
            ASSERT_NE(fused, nullptr);
            ASSERT_NE(plain, nullptr);
            EXPECT_EQ(CountOf(*fused, expected), 1U) << String(text).c_str() << "\n" << fused->Disassemble().c_str();
            EXPECT_EQ(CountFused(*plain), 0U) << plain->Disassemble().c_str();
            EXPECT_LT(fused->Code().size(), plain->Code().size()) << String(text).c_str();
            EXPECT_EQ(fused->Reads(), plain->Reads()) << String(text).c_str();
            Generator generator;
            for (size_t i = 0; i < iterations; ++i) {
                Randomize(world.a, generator);
                Randomize(world.b, generator);
                const T withFusion = Run<T>(*fused, world.context);
                const T without = Run<T>(*plain, world.context);
                ASSERT_EQ(std::memcmp(&withFusion, &without, sizeof(T)), 0) << String(text).c_str() << " input " << i;
            }
        }

        /// The code the lowering writes for `slot * k + b` over the register pair r0, r1, with the load, the two constants and the result in r0
        Vector<Instruction> MulAddCode(Opcode load, Opcode mul, Opcode add, uint8_t level = 0, uint32_t offset = 4) {
            return {Instruction{load, 0, level, 0, 0, offset}, Instruction{Opcode::LoadConst, 1, 0, 0, 0, 0}, Instruction{mul, 0, 0, 1, 0, 0}, Instruction{Opcode::LoadConst, 1, 0, 0, 0, 1},
                    Instruction{add, 0, 0, 1, 0, 0}};
        }

        Vector<Instruction> Int32MulAddCode() {
            return MulAddCode(Opcode::LoadSlotInt32, Opcode::MulInt32, Opcode::AddInt32);
        }

    }  // namespace

    TEST(ProgramFusionTest, MulAddOverEachNumberTypeIsOneInstructionWithTheSameBits) {
        ExpectSameBits<int32_t>("a.i32 * 3 + 7", Opcode::MulAddSlotInt32);
        ExpectSameBits<int32_t>("b.i32b * -2147483647 + 2147483647", Opcode::MulAddSlotInt32);
        ExpectSameBits<int64_t>("a.i64 * 3 + 7", Opcode::MulAddSlotInt64);
        ExpectSameBits<int64_t>("b.i64b * 4611686018427387904 + -1", Opcode::MulAddSlotInt64);
        ExpectSameBits<float>("a.f32 * 0.1 + 0.2", Opcode::MulAddSlotFloat);
        ExpectSameBits<float>("b.f32b * 1e30 + -1e30", Opcode::MulAddSlotFloat);
        ExpectSameBits<double>("a.f64 * 0.1 + 0.2", Opcode::MulAddSlotDouble);
        ExpectSameBits<double>("b.f64b * 1e300 + 1e-300", Opcode::MulAddSlotDouble);
    }

    TEST(ProgramFusionTest, TheProductIsRoundedBeforeTheSumLikeTwoInstructions) {
        // (1 + 2^-23)^2 does not fit a float: rounded it is 1 + 2^-22, which `-(1 + 2^-22)` cancels exactly; a fused multiply-add keeps the 2^-46 and does not
        FusionWorld world;
        const Program* fusedFloat = Compile(world, "a.f32 * 1.0000001 + -1.0000002", ProgramType::Float, true, DescribeType<float>());
        ASSERT_NE(fusedFloat, nullptr);
        ASSERT_EQ(CountOf(*fusedFloat, Opcode::MulAddSlotFloat), 1U);
        world.a.f32 = 1.0f + std::numeric_limits<float>::epsilon();
        EXPECT_EQ(Run<float>(*fusedFloat, world.context), 0.0f);

        const Program* fusedDouble = Compile(world, "a.f64 * 1.0000000000000002 + -1.0000000000000004", ProgramType::Double, true, DescribeType<double>());
        ASSERT_NE(fusedDouble, nullptr);
        ASSERT_EQ(CountOf(*fusedDouble, Opcode::MulAddSlotDouble), 1U);
        world.a.f64 = 1.0 + std::numeric_limits<double>::epsilon();
        EXPECT_EQ(Run<double>(*fusedDouble, world.context), 0.0);
    }

    TEST(ProgramFusionTest, TheIntegersWrapAsTheInstructionsDo) {
        FusionWorld world;
        const Program* program = Compile(world, "a.i32 * 2 + 5", ProgramType::Int32, true, DescribeType<int32_t>());
        ASSERT_NE(program, nullptr);
        ASSERT_EQ(CountOf(*program, Opcode::MulAddSlotInt32), 1U);
        world.a.i32 = std::numeric_limits<int32_t>::max();
        EXPECT_EQ(Run<int32_t>(*program, world.context), 3);
        world.a.i32 = std::numeric_limits<int32_t>::min();
        EXPECT_EQ(Run<int32_t>(*program, world.context), 5);
    }

    TEST(ProgramFusionTest, ItFusesInsideALargerFormulaAndTheOtherPartsStayAsTheyWere) {
        ExpectSameBits<int32_t>("min(a.i32 * 3 + 7, b.i32)", Opcode::MulAddSlotInt32);
        ExpectSameBits<float>("max(a.f32b, b.f32 * 0.5 + 1.0) + a.f32", Opcode::MulAddSlotFloat);

        FusionWorld world;
        const Program* program = Compile(world, "min(a.i32 * 3 + 7, b.i32)", ProgramType::Int32, true, DescribeType<int32_t>());
        ASSERT_NE(program, nullptr);
        EXPECT_EQ(CountOf(*program, Opcode::MinInt32), 1U);
        EXPECT_EQ(CountOf(*program, Opcode::LoadSlotInt32), 1U);
    }

    TEST(ProgramFusionTest, ASlotAsTheAddendStaysUnfused) {
        FusionWorld world;
        const Program* program = Compile(world, "a.i32 * 3 + a.i32b", ProgramType::Int32, true, DescribeType<int32_t>());
        ASSERT_NE(program, nullptr);
        EXPECT_EQ(CountFused(*program), 0U) << program->Disassemble().c_str();
    }

    TEST(ProgramFusionTest, TwoFormulasAreTwoProgramsAndTheFusedAndTheUnfusedAreTwoEntries) {
        FusionWorld world;
        const Program* fused = Compile(world, "a.i32 * 3 + 7", ProgramType::Int32, true, DescribeType<int32_t>());
        const Program* plain = Compile(world, "a.i32 * 3 + 7", ProgramType::Int32, false, DescribeType<int32_t>());
        const Program* again = Compile(world, "a.i32 * 3 + 7", ProgramType::Int32, true, DescribeType<int32_t>());
        ASSERT_NE(fused, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_NE(fused, plain);
        EXPECT_EQ(fused, again);
        EXPECT_FALSE(fused->SameCode(*plain));
    }

    TEST(ProgramFusionTest, TheFusedProgramReadsTheLevelOfItsSlot) {
        FusionWorld world;
        const Program* fused = Compile(world, "b.i32 * 2 + 1", ProgramType::Int32, true, DescribeType<int32_t>());
        const Program* plain = Compile(world, "b.i32 * 2 + 1", ProgramType::Int32, false, DescribeType<int32_t>());
        ASSERT_NE(fused, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(fused->Reads(), LevelBit(1));
        EXPECT_EQ(fused->Reads(), plain->Reads());
    }

    TEST(ProgramFusionTest, TheFusedInstructionIsPrintedWithItsFieldAndConstants) {
        FusionWorld world;
        const Program* program = Compile(world, "a.i32 * 3 + 7", ProgramType::Int32, true, DescribeType<int32_t>());
        ASSERT_NE(program, nullptr);
        const String text = program->Disassemble();
        EXPECT_NE(text.find("MulAddSlotInt32"), String::npos) << text.c_str();
        EXPECT_NE(text.find("level 0"), String::npos) << text.c_str();
    }

    // The sequences below are written by hand: the lowering never writes most of them, and `Fuse` reads only the code

    TEST(ProgramFusionTest, TheCanonicalSequenceBecomesOneInstruction) {
        Vector<Instruction> code = Int32MulAddCode();
        Fuse(code, 0);
        ASSERT_EQ(code.size(), 1U);
        EXPECT_EQ(code[0], (Instruction{Opcode::MulAddSlotInt32, 0, 0, 0, 1, 4}));
    }

    TEST(ProgramFusionTest, AConstantIndexOverAByteLeavesTheSequenceAlone) {
        Vector<Instruction> code = Int32MulAddCode();
        code[1].imm = kMaxFusedConstantIndex;
        Vector<Instruction> fits = code;
        Fuse(fits, 0);
        EXPECT_EQ(fits.size(), 1U);

        code[1].imm = kMaxFusedConstantIndex + 1;
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);

        code[1].imm = 0;
        code[3].imm = kMaxFusedConstantIndex + 1;
        unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);
    }

    TEST(ProgramFusionTest, ADroppedTemporaryThatIsReadLaterKeepsTheSequence) {
        // r1 held the addend; a later instruction reads it, so dropping its write would change that instruction's operand
        Vector<Instruction> code = Int32MulAddCode();
        code.push_back(Instruction{Opcode::AddInt32, 2, 0, 1, 0, 0});
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 2);
        EXPECT_EQ(unchanged, code);

        // The same register written again before it is read is dead at the end of the sequence
        Vector<Instruction> rewritten = Int32MulAddCode();
        rewritten.push_back(Instruction{Opcode::LoadConst, 1, 0, 0, 0, 0});
        rewritten.push_back(Instruction{Opcode::AddInt32, 2, 0, 1, 0, 0});
        Fuse(rewritten, 2);
        EXPECT_EQ(rewritten.size(), 3U);
        EXPECT_EQ(rewritten[0].op, Opcode::MulAddSlotInt32);
    }

    TEST(ProgramFusionTest, ADroppedTemporaryThatIsTheResultKeepsTheSequence) {
        Vector<Instruction> code = Int32MulAddCode();
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 1);
        EXPECT_EQ(unchanged, code);

        Vector<Instruction> fused = code;
        Fuse(fused, 0);
        EXPECT_EQ(fused.size(), 1U);
    }

    TEST(ProgramFusionTest, ARegisterRangeThatAPackReadsCountsAsRead) {
        // `Pack` of a `Vec3` reads r1, r2 and r3 though only `a` names it
        Vector<Instruction> code = MulAddCode(Opcode::LoadSlotFloat, Opcode::MulFloat, Opcode::AddFloat);
        code.push_back(Instruction{Opcode::LoadConst, 2, 0, 0, 0, 0});
        code.push_back(Instruction{Opcode::LoadConst, 3, 0, 0, 0, 0});
        code.push_back(Instruction{Opcode::PackVec3, 4, 1, 0, 0, 0});
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 4);
        EXPECT_EQ(unchanged, code);
    }

    TEST(ProgramFusionTest, ASequenceWhoseRegistersDoNotChainIsLeftAlone) {
        // The addition reads r2, not the product in r0: the shape is the same, the meaning is not
        Vector<Instruction> code = Int32MulAddCode();
        code[4].a = 2;
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);

        // The second constant overwrites the product
        code = Int32MulAddCode();
        code[3].dst = 0;
        unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);

        // The multiplication reads the field twice (the constant lands over it)
        code = Int32MulAddCode();
        code[1].dst = 0;
        code[2].b = 0;
        unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);
    }

    TEST(ProgramFusionTest, ATypeMismatchBetweenTheInstructionsIsNotFused) {
        Vector<Instruction> code = MulAddCode(Opcode::LoadSlotInt32, Opcode::MulInt32, Opcode::AddInt64);
        Vector<Instruction> unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);

        code = MulAddCode(Opcode::LoadSlotBool, Opcode::MulInt32, Opcode::AddInt32);
        unchanged = code;
        Fuse(unchanged, 0);
        EXPECT_EQ(unchanged, code);
    }

    TEST(ProgramFusionTest, TwoSequencesInARowAreBothFused) {
        Vector<Instruction> first = Int32MulAddCode();
        Vector<Instruction> second = MulAddCode(Opcode::LoadSlotInt32, Opcode::MulInt32, Opcode::AddInt32, 1, 8);
        for (Instruction& ins : second) {
            if (ins.dst == 0) {
                ins.dst = 2;
            }
            if (ins.a == 0 && ins.op != Opcode::LoadSlotInt32) {
                ins.a = 2;
            }
            if (ins.dst == 1) {
                ins.dst = 3;
            }
            if (ins.b == 1) {
                ins.b = 3;
            }
        }
        Vector<Instruction> code = first;
        code.insert(code.end(), second.begin(), second.end());
        code.push_back(Instruction{Opcode::AddInt32, 0, 0, 2, 0, 0});
        Fuse(code, 0);
        ASSERT_EQ(code.size(), 3U);
        EXPECT_EQ(code[0].op, Opcode::MulAddSlotInt32);
        EXPECT_EQ(code[1].op, Opcode::MulAddSlotInt32);
        EXPECT_EQ(code[1].dst, 2U);
    }

    TEST(ProgramFusionTest, AFinishedProgramOfFusedCodeIsRefusedWhenItsFieldsAreNot) {
        ProgramBuilder builder;
        const uint8_t reg = builder.AcquireRegister().value();
        const uint32_t k = builder.AddConstant(int32_t{3});
        builder.Emit(Opcode::MulAddSlotInt32, reg, 0, static_cast<uint8_t>(k), static_cast<uint8_t>(k + 1), 4);
        EXPECT_FALSE(builder.Finish(ProgramType::Int32, reg).has_value());

        ProgramBuilder good;
        const uint8_t out = good.AcquireRegister().value();
        const uint32_t constK = good.AddConstant(int32_t{3});
        const uint32_t constB = good.AddConstant(int32_t{7});
        good.Emit(Opcode::MulAddSlotInt32, out, 1, static_cast<uint8_t>(constK), static_cast<uint8_t>(constB), 4);
        const auto program = good.Finish(ProgramType::Int32, out);
        ASSERT_TRUE(program.has_value());
        EXPECT_EQ(program->Reads(), LevelBit(1));

        ProgramBuilder tooHigh;
        const uint8_t high = tooHigh.AcquireRegister().value();
        static_cast<void>(tooHigh.AddConstant(int32_t{3}));
        tooHigh.Emit(Opcode::MulAddSlotInt32, high, static_cast<uint8_t>(kMaxContextLevels), 0, 0, 4);
        EXPECT_FALSE(tooHigh.Finish(ProgramType::Int32, high).has_value());
    }

}  // namespace BECore::Tests
