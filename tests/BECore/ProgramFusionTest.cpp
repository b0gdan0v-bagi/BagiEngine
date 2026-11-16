// The peephole pass (Values/Programs/Peephole.h): each fusion computes what the unfused code computes, bit for bit, and is left alone where its rules say so.

#include "ProgramFusionTest.h"

#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Peephole.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <BECore/Values/ValueTables.h>
#include <Common/Printers.h>
#include <Generated/ProgramFusionTest.gen.hpp>
#include <bit>
#include <cmath>
#include <gtest/gtest.h>

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
            stats.dir = Vec3{Generate<float>(generator), Generate<float>(generator), Generate<float>(generator)};
            stats.dirb = Vec3{Generate<float>(generator), Generate<float>(generator), Generate<float>(generator)};
        }

        /// Every instruction `Fuse` writes
        bool IsFused(Opcode op) {
            return IsMulAddSlot(op) || IsLerpSlots(op) || op == Opcode::LoadTableSlot;
        }

        size_t CountOf(const Program& program, Opcode op) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [op](const Instruction& ins) { return ins.op == op; }));
        }

        size_t CountFused(const Program& program) {
            return static_cast<size_t>(std::count_if(program.Code().begin(), program.Code().end(), [](const Instruction& ins) { return IsFused(ins.op); }));
        }

        const Program* Compile(const FusionWorld& world, StringView text, ProgramType type, bool fuse, const TypeDesc& wanted, const IValueTables* tables = nullptr) {
            CompileOptions options;
            options.fuse = fuse;
            options.tables = tables;
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
        void ExpectSameBits(StringView text, Opcode expected, const IValueTables* tables = nullptr, size_t iterations = kInputs) {
            FusionWorld world;
            const Program* fused = Compile(world, text, kProgramTypeOf<T>, true, DescribeType<T>(), tables);
            const Program* plain = Compile(world, text, kProgramTypeOf<T>, false, DescribeType<T>(), tables);
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

        /// `lerp(a-field, b-field, t)` as the lowering writes it when the weight is one constant: loads into r0 and r1, the weight in r2
        Vector<Instruction> LerpCode(Opcode load = Opcode::LoadSlotFloat, Opcode lerp = Opcode::LerpFloat) {
            return {Instruction{load, 0, 0, 0, 0, 4}, Instruction{load, 1, 1, 0, 0, 8}, Instruction{Opcode::LoadConst, 2, 0, 0, 0, 0}, Instruction{lerp, 0, 0, 1, 2, 0}};
        }

        /// A table over a field as the lowering writes it: the index into r0, the table of @p count constants from constant 3 over it
        Vector<Instruction> TableCode(uint32_t first = 3, uint32_t count = 5) {
            Instruction table{Opcode::LoadTable, 0, 0, 0, 0, first};
            SetTableEntryCount(table, count);
            return {Instruction{Opcode::LoadSlotInt32, 0, 1, 0, 0, 8}, table};
        }

        void ExpectUnchanged(Vector<Instruction> code, uint8_t resultRegister) {
            const Vector<Instruction> before = code;
            Fuse(code, resultRegister);
            EXPECT_EQ(code, before);
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

    TEST(ProgramFusionTest, MulAddOverAVec3IsOneInstructionWithTheSameBits) {
        ExpectSameBits<Vec3>("a.dir * 2 + vec3(1, 2, 3)", Opcode::MulAddSlotVec3);
        ExpectSameBits<Vec3>("b.dirb * 0.1 + vec3(0.2, -0.0, 1e30)", Opcode::MulAddSlotVec3);
        ExpectSameBits<Vec3>("a.dir * 1e30 + vec3(-1e30, 1e-30, 0)", Opcode::MulAddSlotVec3);
        ExpectSameBits<Vec3>("b.dir * 0 + vec3(0, 0, 0)", Opcode::MulAddSlotVec3);
    }

    TEST(ProgramFusionTest, TheVec3ProductIsRoundedBeforeTheSumLikeTwoInstructions) {
        // The same (1 + 2^-23)^2 cancellation as the scalar case, componentwise: a fused multiply-add of any component would leave 2^-46
        FusionWorld world;
        const Program* program = Compile(world, "a.dir * 1.0000001 + vec3(-1.0000002, -1.0000002, -1.0000002)", ProgramType::Vec3, true, DescribeType<Vec3>());
        ASSERT_NE(program, nullptr);
        ASSERT_EQ(CountOf(*program, Opcode::MulAddSlotVec3), 1U);
        const float step = 1.0f + std::numeric_limits<float>::epsilon();
        world.a.dir = Vec3{step, step, step};
        EXPECT_EQ(Run<Vec3>(*program, world.context), (Vec3{0.0f, 0.0f, 0.0f}));
    }

    TEST(ProgramFusionTest, AVec3MulAddFusesInsideALargerFormulaAndTheOtherPartsStayAsTheyWere) {
        ExpectSameBits<Vec3>("(a.dir * 2 + vec3(1, 2, 3)) - b.dirb", Opcode::MulAddSlotVec3);
        ExpectSameBits<float>("length(b.dir * 0.5 + vec3(1, 2, 3)) + a.f32", Opcode::MulAddSlotVec3);

        FusionWorld world;
        const Program* program = Compile(world, "(a.dir * 2 + vec3(1, 2, 3)) - b.dirb", ProgramType::Vec3, true, DescribeType<Vec3>());
        ASSERT_NE(program, nullptr);
        EXPECT_EQ(CountOf(*program, Opcode::SubVec3), 1U);
        EXPECT_EQ(CountOf(*program, Opcode::LoadSlotVec3), 1U);
    }

    TEST(ProgramFusionTest, AVec3ShapeThatIsNotSlotTimesFloatPlusConstantStaysUnfused) {
        FusionWorld world;
        // A slot as the addend, the scalar on the left (`MulFloatVec3`), a slot as the factor's own product and a formula as the factor are other shapes
        for (const StringView text :
             {StringView{"a.dir * 2 + a.dirb"}, StringView{"2 * a.dir + vec3(1, 2, 3)"}, StringView{"a.dir * a.f32 + vec3(1, 2, 3)"}, StringView{"a.dir * 2 - vec3(1, 2, 3)"}}) {
            const Program* program = Compile(world, text, ProgramType::Vec3, true, DescribeType<Vec3>());
            ASSERT_NE(program, nullptr);
            EXPECT_EQ(CountOf(*program, Opcode::MulAddSlotVec3), 0U) << String(text).c_str() << "\n" << program->Disassemble().c_str();
        }
    }

    TEST(ProgramFusionTest, TheVec3MulAddReadsTheLevelOfItsSlotAndIsPrinted) {
        FusionWorld world;
        const Program* fused = Compile(world, "b.dir * 2 + vec3(1, 2, 3)", ProgramType::Vec3, true, DescribeType<Vec3>());
        const Program* plain = Compile(world, "b.dir * 2 + vec3(1, 2, 3)", ProgramType::Vec3, false, DescribeType<Vec3>());
        ASSERT_NE(fused, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(fused->Reads(), LevelBit(1));
        EXPECT_EQ(fused->Reads(), plain->Reads());
        EXPECT_EQ(CountFused(*plain), 0U);
        const String text = fused->Disassemble();
        EXPECT_NE(text.find("MulAddSlotVec3"), String::npos) << text.c_str();
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

    TEST(ProgramFusionTest, TheVec3SequenceByHandBecomesOneInstructionOnlyWithItsOwnOpcodes) {
        Vector<Instruction> code = MulAddCode(Opcode::LoadSlotVec3, Opcode::MulVec3Float, Opcode::AddVec3);
        Fuse(code, 0);
        ASSERT_EQ(code.size(), 1U);
        EXPECT_EQ(code[0], (Instruction{Opcode::MulAddSlotVec3, 0, 0, 0, 1, 4}));

        // The scalar on the left, a `Vec4` sum and a `Vec4` field are other rows
        ExpectUnchanged(MulAddCode(Opcode::LoadSlotVec3, Opcode::MulFloatVec3, Opcode::AddVec3), 0);
        ExpectUnchanged(MulAddCode(Opcode::LoadSlotVec3, Opcode::MulVec3Float, Opcode::AddVec4), 0);
        ExpectUnchanged(MulAddCode(Opcode::LoadSlotVec4, Opcode::MulVec3Float, Opcode::AddVec3), 0);

        // The factor register is read later: the write the fusion drops is not dead
        Vector<Instruction> later = MulAddCode(Opcode::LoadSlotVec3, Opcode::MulVec3Float, Opcode::AddVec3);
        later.push_back(Instruction{Opcode::AddVec3, 2, 0, 1, 0, 0});
        ExpectUnchanged(later, 2);
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

    TEST(ProgramFusionTest, AFinishedProgramOfVec3FusedCodeIsRefusedWhenItsConstantsAreNot) {
        ProgramBuilder builder;
        const uint8_t reg = builder.AcquireRegister().value();
        const uint32_t k = builder.AddConstant(1.0f);
        builder.Emit(Opcode::MulAddSlotVec3, reg, 0, static_cast<uint8_t>(k), static_cast<uint8_t>(k + 1), 4);
        EXPECT_FALSE(builder.Finish(ProgramType::Vec3, reg).has_value());

        ProgramBuilder good;
        const uint8_t out = good.AcquireRegister().value();
        const uint32_t constK = good.AddConstant(2.0f);
        const uint32_t constB = good.AddConstant(Vec3{1.0f, 2.0f, 3.0f});
        good.Emit(Opcode::MulAddSlotVec3, out, 1, static_cast<uint8_t>(constK), static_cast<uint8_t>(constB), 4);
        const auto program = good.Finish(ProgramType::Vec3, out);
        ASSERT_TRUE(program.has_value());
        EXPECT_EQ(program->Reads(), LevelBit(1));
    }

    TEST(ProgramFusionTest, ALerpOfTwoSlotsIsOneInstructionWithTheSameBits) {
        ExpectSameBits<float>("lerp(a.f32, b.f32b, a.f32b)", Opcode::LerpSlotsFloat);
        ExpectSameBits<float>("lerp(b.f32, b.f32b, 0.25)", Opcode::LerpSlotsFloat);
        ExpectSameBits<double>("lerp(a.f64, b.f64b, a.f64b)", Opcode::LerpSlotsDouble);
        ExpectSameBits<double>("lerp(b.f64, a.f64, 0.5)", Opcode::LerpSlotsDouble);
        // A weight that is a formula of its own, fused itself, stays in place between the two
        ExpectSameBits<float>("lerp(a.f32, b.f32b, b.f32 * 0.5 + 0.25)", Opcode::LerpSlotsFloat);
    }

    TEST(ProgramFusionTest, ALerpInsideTheWeightOfAnotherIsFusedToo) {
        FusionWorld world;
        const StringView text = "lerp(a.f64, b.f64b, lerp(a.f64b, b.f64, b.f64b))";
        const Program* fused = Compile(world, text, ProgramType::Double, true, DescribeType<double>());
        const Program* plain = Compile(world, text, ProgramType::Double, false, DescribeType<double>());
        ASSERT_NE(fused, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(CountOf(*fused, Opcode::LerpSlotsDouble), 2U) << fused->Disassemble().c_str();
        Generator generator;
        for (size_t i = 0; i < kInputs; ++i) {
            Randomize(world.a, generator);
            Randomize(world.b, generator);
            const double withFusion = Run<double>(*fused, world.context);
            const double without = Run<double>(*plain, world.context);
            ASSERT_EQ(std::memcmp(&withFusion, &without, sizeof(double)), 0) << i;
        }
    }

    TEST(ProgramFusionTest, ALerpIsExactAtTheEndsAndDoesNotClampTheWeight) {
        FusionWorld world;
        const Program* program = Compile(world, "lerp(a.f32, b.f32, a.f32b)", ProgramType::Float, true, DescribeType<float>());
        ASSERT_NE(program, nullptr);
        ASSERT_EQ(CountOf(*program, Opcode::LerpSlotsFloat), 1U);
        world.a.f32 = 0.1f;
        world.b.f32 = 7.0f;
        world.a.f32b = 0.0f;
        EXPECT_EQ(Run<float>(*program, world.context), 0.1f);
        world.a.f32b = 1.0f;
        EXPECT_EQ(Run<float>(*program, world.context), 7.0f);
        world.a.f32b = 2.0f;
        EXPECT_EQ(Run<float>(*program, world.context), std::lerp(0.1f, 7.0f, 2.0f));
    }

    TEST(ProgramFusionTest, ALerpWithOneSlotOrOfAVectorStaysUnfused) {
        FusionWorld world;
        const Program* oneSlot = Compile(world, "lerp(a.f32, 5.0, a.f32b)", ProgramType::Float, true, DescribeType<float>());
        ASSERT_NE(oneSlot, nullptr);
        EXPECT_EQ(CountFused(*oneSlot), 0U) << oneSlot->Disassemble().c_str();
        const Program* otherSlot = Compile(world, "lerp(5.0, a.f32, a.f32b)", ProgramType::Float, true, DescribeType<float>());
        ASSERT_NE(otherSlot, nullptr);
        EXPECT_EQ(CountFused(*otherSlot), 0U) << otherSlot->Disassemble().c_str();
        const Program* vectors = Compile(world, "lerp(a.dir, b.dirb, a.f32)", ProgramType::Vec3, true, DescribeType<Vec3>());
        ASSERT_NE(vectors, nullptr);
        EXPECT_EQ(CountFused(*vectors), 0U) << vectors->Disassemble().c_str();
    }

    TEST(ProgramFusionTest, ALerpReadsTheLevelOfBothSlots) {
        FusionWorld world;
        const Program* fused = Compile(world, "lerp(a.f32, b.f32b, 0.25)", ProgramType::Float, true, DescribeType<float>());
        const Program* plain = Compile(world, "lerp(a.f32, b.f32b, 0.25)", ProgramType::Float, false, DescribeType<float>());
        ASSERT_NE(fused, nullptr);
        ASSERT_NE(plain, nullptr);
        EXPECT_EQ(CountOf(*fused, Opcode::LerpSlotsFloat), 1U);
        EXPECT_EQ(fused->Reads(), static_cast<LevelMask>(LevelBit(0) | LevelBit(1)));
        EXPECT_EQ(fused->Reads(), plain->Reads());
        const Program* second = Compile(world, "lerp(b.f32, b.f32b, 0.25)", ProgramType::Float, true, DescribeType<float>());
        ASSERT_NE(second, nullptr);
        EXPECT_EQ(second->Reads(), LevelBit(1));
    }

    TEST(ProgramFusionTest, TheLerpSequenceByHandKeepsTheCodeOfTheWeightWhereItIs) {
        Vector<Instruction> code = LerpCode();
        Fuse(code, 0);
        ASSERT_EQ(code.size(), 2U);
        EXPECT_EQ(code[0].op, Opcode::LoadConst);
        EXPECT_EQ(code[1], (Instruction{Opcode::LerpSlotsFloat, 0, 0, 1, 2, PackHalves(4, 8)}));
    }

    TEST(ProgramFusionTest, TheWeightThatTouchesAFusedRegisterKeepsTheLerp) {
        // The weight reads r1, which the fusion no longer fills
        Vector<Instruction> reads = LerpCode();
        reads[2] = Instruction{Opcode::Copy, 2, 1, 0, 0, 0};
        ExpectUnchanged(reads, 0);
        // The weight overwrites r0, which the `Lerp` reads as its first operand
        Vector<Instruction> writes = LerpCode();
        writes[2] = Instruction{Opcode::LoadConst, 0, 0, 0, 0, 0};
        writes[3].c = 0;
        writes[3].a = 0;
        ExpectUnchanged(writes, 0);
        // The weight is one of the two slots
        Vector<Instruction> weight = LerpCode();
        weight[3].c = 1;
        ExpectUnchanged(weight, 0);
    }

    TEST(ProgramFusionTest, ASlotRegisterThatIsReadLaterOrIsTheResultKeepsTheLerp) {
        Vector<Instruction> later = LerpCode();
        later.push_back(Instruction{Opcode::AddFloat, 3, 0, 1, 0, 0});
        ExpectUnchanged(later, 3);

        Vector<Instruction> rewritten = LerpCode();
        rewritten.push_back(Instruction{Opcode::LoadConst, 1, 0, 0, 0, 0});
        rewritten.push_back(Instruction{Opcode::AddFloat, 3, 0, 1, 0, 0});
        Fuse(rewritten, 3);
        EXPECT_EQ(rewritten.size(), 4U);
        EXPECT_EQ(rewritten[1].op, Opcode::LerpSlotsFloat);

        Vector<Instruction> result = LerpCode();
        ExpectUnchanged(result, 1);
    }

    TEST(ProgramFusionTest, AnOffsetThatDoesNotFitAHalfLeavesTheLerpAndTheTable) {
        Vector<Instruction> fits = LerpCode();
        fits[0].imm = kMaxFusedHalf;
        fits[1].imm = kMaxFusedHalf;
        Fuse(fits, 0);
        EXPECT_EQ(fits.size(), 2U);
        EXPECT_EQ(fits[1], (Instruction{Opcode::LerpSlotsFloat, 0, 0, 1, 2, PackHalves(kMaxFusedHalf, kMaxFusedHalf)}));

        Vector<Instruction> tooFar = LerpCode();
        tooFar[1].imm = kMaxFusedHalf + 1;
        ExpectUnchanged(tooFar, 0);

        Vector<Instruction> table = TableCode();
        table[0].imm = kMaxFusedHalf + 1;
        ExpectUnchanged(table, 0);
        table = TableCode(kMaxFusedHalf + 1);
        ExpectUnchanged(table, 0);
        table = TableCode(kMaxFusedHalf);
        Fuse(table, 0);
        EXPECT_EQ(table.size(), 1U);
    }

    TEST(ProgramFusionTest, ALerpOfAnotherTypeOrInsideAnotherLerpFusesOnlyWhatItMatches) {
        ExpectUnchanged(LerpCode(Opcode::LoadSlotVec3, Opcode::LerpVec3), 0);
        ExpectUnchanged(LerpCode(Opcode::LoadSlotInt32, Opcode::LerpFloat), 0);

        // lerp(a, b, lerp(c, d, e)): r0 r1 outside, r2 r3 inside, the weight of the inner in r4
        const Vector<Instruction> code = {Instruction{Opcode::LoadSlotFloat, 0, 0, 0, 0, 0},  Instruction{Opcode::LoadSlotFloat, 1, 0, 0, 0, 4},  Instruction{Opcode::LoadSlotFloat, 2, 0, 0, 0, 8},
                                          Instruction{Opcode::LoadSlotFloat, 3, 0, 0, 0, 12}, Instruction{Opcode::LoadSlotFloat, 4, 0, 0, 0, 16}, Instruction{Opcode::LerpFloat, 2, 2, 3, 4, 0},
                                          Instruction{Opcode::LerpFloat, 0, 0, 1, 2, 0}};
        Vector<Instruction> fused = code;
        Fuse(fused, 0);
        ASSERT_EQ(fused.size(), 3U);
        EXPECT_EQ(fused[0].op, Opcode::LoadSlotFloat);
        EXPECT_EQ(fused[1], (Instruction{Opcode::LerpSlotsFloat, 2, 0, 0, 4, PackHalves(8, 12)}));
        EXPECT_EQ(fused[2], (Instruction{Opcode::LerpSlotsFloat, 0, 0, 0, 2, PackHalves(0, 4)}));
    }

    TEST(ProgramFusionTest, ATableIndexedByASlotIsOneInstructionWithTheSameBits) {
        ValueTableSet tables;
        tables.Add("Damage", {10.0, 20.0, 30.0, 45.0});
        tables.Add("Rates", {0.5, 0.25});
        ExpectSameBits<int32_t>("at(a.i32, 10, 20, 30, 40, 50)", Opcode::LoadTableSlot);
        ExpectSameBits<int32_t>("at(b.i32b, 7)", Opcode::LoadTableSlot);
        ExpectSameBits<int32_t>("at(a.i32, -5, 0, 5) * 3", Opcode::LoadTableSlot);
        ExpectSameBits<float>("at(a.i32, 0.5, 1.5, 2.5)", Opcode::LoadTableSlot);
        ExpectSameBits<int32_t>("ByLevel('Damage', b.i32)", Opcode::LoadTableSlot, &tables);
        ExpectSameBits<int64_t>("ByLevel('Damage', a.i32) + 1", Opcode::LoadTableSlot, &tables);
        ExpectSameBits<double>("ByLevel('Rates', a.i32)", Opcode::LoadTableSlot, &tables);
        ExpectSameBits<Vec3>("at(a.i32, vec3(1, 0, 0), vec3(0, 1, 0))", Opcode::LoadTableSlot);
    }

    TEST(ProgramFusionTest, TableCountsAndConstantIndexesAreNoRegistersInTheFusedFields) {
        // The count of a table and the constant indexes of a MulAddSlot sit in b and c: values above the 32 registers are valid there
        String entries;
        for (int32_t i = 1; i <= 40; ++i) {
            entries += Format(", {}", i * 10);
        }
        ExpectSameBits<int32_t>(Format("at(a.i32{})", entries), Opcode::LoadTableSlot, nullptr, 200);
        ExpectSameBits<int32_t>(Format("at(a.i32{}) + (b.i32b * 3 + 7)", entries), Opcode::MulAddSlotInt32, nullptr, 200);

        ValueTableSet tables;
        Vector<double> big;
        for (int32_t i = 0; i < 100; ++i) {
            big.push_back(i * 1.5 + 0.25);
        }
        tables.Add("Big", big);
        ExpectSameBits<double>("ByLevel('Big', a.i32)", Opcode::LoadTableSlot, &tables, 200);
        ExpectSameBits<double>("ByLevel('Big', a.i32) + (b.f64b * 3 + 7)", Opcode::MulAddSlotDouble, &tables, 200);
    }

    TEST(ProgramFusionTest, ATableOverASlotClampsTheIndexToTheEnds) {
        ValueTableSet tables;
        tables.Add("Damage", {10.0, 20.0, 30.0});
        FusionWorld world;
        const Program* program = Compile(world, "ByLevel('Damage', a.i32)", ProgramType::Int32, true, DescribeType<int32_t>(), &tables);
        ASSERT_NE(program, nullptr);
        ASSERT_EQ(CountOf(*program, Opcode::LoadTableSlot), 1U);
        const Pair<int32_t, int32_t> cases[] = {{std::numeric_limits<int32_t>::min(), 10}, {-1, 10}, {0, 10}, {1, 20}, {2, 30}, {3, 30}, {std::numeric_limits<int32_t>::max(), 30}};
        for (const auto& [index, expected] : cases) {
            world.a.i32 = index;
            EXPECT_EQ(Run<int32_t>(*program, world.context), expected) << index;
        }
        EXPECT_EQ(program->Reads(), LevelBit(0));
    }

    TEST(ProgramFusionTest, ATableSequenceByHandAndWhatKeepsItUnfused) {
        Vector<Instruction> code = TableCode();
        Fuse(code, 0);
        ASSERT_EQ(code.size(), 1U);
        Instruction expected{Opcode::LoadTableSlot, 0, 1, 0, 0, PackHalves(8, 3)};
        SetTableEntryCount(expected, 5);
        EXPECT_EQ(code[0], expected);

        // The index register is read after the table instruction: its load must stay
        Vector<Instruction> later = TableCode();
        later[1].dst = 1;
        later.push_back(Instruction{Opcode::AddInt32, 2, 0, 1, 0, 0});
        ExpectUnchanged(later, 2);

        // Another type of index, and a table whose index is not the loaded register
        Vector<Instruction> wide = TableCode();
        wide[0].op = Opcode::LoadSlotInt64;
        ExpectUnchanged(wide, 0);
        Vector<Instruction> other = TableCode();
        other[1].a = 2;
        ExpectUnchanged(other, 0);
    }

    TEST(ProgramFusionTest, FinishCountsAndChecksTheFieldsOfTheNewInstructions) {
        ProgramBuilder builder;
        const uint8_t reg = builder.AcquireRegister().value();
        const uint8_t weight = builder.AcquireRegister().value();
        builder.EmitLoadConst(weight, 0.5f);
        builder.Emit(Opcode::LerpSlotsFloat, reg, 1, 2, weight, PackHalves(0, 4));
        const auto program = builder.Finish(ProgramType::Float, reg);
        ASSERT_TRUE(program.has_value());
        EXPECT_EQ(program->Reads(), static_cast<LevelMask>(LevelBit(1) | LevelBit(2)));

        ProgramBuilder beyond;
        const uint8_t out = beyond.AcquireRegister().value();
        beyond.Emit(Opcode::LerpSlotsFloat, out, 0, static_cast<uint8_t>(kMaxContextLevels), out, 0);
        EXPECT_FALSE(beyond.Finish(ProgramType::Float, out).has_value());

        ProgramBuilder outside;
        const uint8_t dst = outside.AcquireRegister().value();
        static_cast<void>(outside.AddConstant(int32_t{1}));
        Instruction table{Opcode::LoadTableSlot, dst, 0, 0, 0, PackHalves(0, 0)};
        SetTableEntryCount(table, 2);
        outside.Emit(table);
        EXPECT_FALSE(outside.Finish(ProgramType::Int32, dst).has_value());

        ProgramBuilder empty;
        const uint8_t result = empty.AcquireRegister().value();
        static_cast<void>(empty.AddConstant(int32_t{1}));
        empty.Emit(Opcode::LoadTableSlot, result, 0, 0, 0, PackHalves(0, 0));
        EXPECT_FALSE(empty.Finish(ProgramType::Int32, result).has_value());

        ProgramBuilder fits;
        const uint8_t value = fits.AcquireRegister().value();
        const Array<int32_t, 2> entries = {5, 6};
        Vector<RegisterImage> images(entries.size());
        std::memcpy(images[0].bytes.data(), &entries[0], sizeof(int32_t));
        std::memcpy(images[1].bytes.data(), &entries[1], sizeof(int32_t));
        const uint32_t first = fits.AddConstantRun(Span<const RegisterImage>(images.data(), images.size()));
        Instruction good{Opcode::LoadTableSlot, value, 1, 0, 0, PackHalves(4, first)};
        SetTableEntryCount(good, 2);
        fits.Emit(good);
        const auto tableProgram = fits.Finish(ProgramType::Int32, value);
        ASSERT_TRUE(tableProgram.has_value());
        EXPECT_EQ(tableProgram->Reads(), LevelBit(1));
        const String text = tableProgram->Disassemble();
        EXPECT_NE(text.find("LoadTableSlot"), String::npos) << text.c_str();
    }

}  // namespace BECore::Tests
