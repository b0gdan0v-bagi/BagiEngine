// The traced run (Values/Programs/Interpreter.h, ProgramTraceSink.h): the sink hears each executed instruction in order, a loop's body once per element, a program that a generated function
// is bound to is traced through the interpreter, and `Run` pays nothing for the hook. The programs are those of the engine's native set (tests/NativePrograms), so each one has a function bound.

#include <BECore/ProgramDomainTypes.h>
#include <BECore/ProgramListTypes.h>
#include <BECore/ProgramRowTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Programs/ProgramTraceSink.h>
#include <BECore/Values/Value.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <NativePrograms/NativeProgramFormulas.h>
#include <NativePrograms/NativeProgramSet.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramDomainData;
    using namespace ProgramListData;
    using namespace ProgramRowData;
    using namespace ValueTestData;

    namespace {

        constexpr int32_t kAttack = 7;
        constexpr int32_t kLevel = 3;

        struct Step {
            uint32_t pc = 0;
            uint32_t nextPc = 0;
            Instruction instruction;
            /// The register the instruction wrote, as it was right after
            RegisterImage written;
        };

        class Recorder final : public ProgramTraceSink {
        public:
            void OnStep(const ProgramTraceStep& step) override {
                steps.push_back(Step{step.pc, step.nextPc, *step.instruction, step.registers[step.instruction->dst]});
            }

            Vector<Step> steps;
        };

        /// The objects the programs of the native set read, under the declaration the set was compiled with
        struct SetWorld {
            ValueUnitStats caster;
            ValueCastInfo cast;
            ProgramRowOperands a;
            ProgramRowOperands b;
            ProgramRowOperands c;
            ProgramListHand hand;
            Context context{NativeProgramSetDeclaration()};

            SetWorld() {
                caster.attack = kAttack;
                cast.level = kLevel;
                a.flag = true;
                a.i32 = 2;
                b.i32 = 5;
                a.narrow = ValueNarrowEnum::Mid;
                a.v3 = Vec3{1.0f, 2.0f, 3.0f};
                context.Set(kCasterLevel, caster);
                context.Set(kCastLevel, cast);
                context.Set(kOperandsLevelA, a);
                context.Set(kOperandsLevelB, b);
                context.Set(kOperandsLevelC, c);
                context.Set(kListsLevel, hand);
                for (size_t i = 0; i < hand.tiles.size(); ++i) {
                    hand.tiles[i].rank = static_cast<int32_t>(i) * 2 - 5;
                }
            }

            SetWorld(const SetWorld&) = delete;
            SetWorld& operator=(const SetWorld&) = delete;
        };

        /// Generated code on, as the benchmark and the native tests leave it, whatever an earlier case did
        class GeneratedOn {
        public:
            GeneratedOn() : _was(NativePrograms::IsEnabled()) {
                NativePrograms::SetEnabled(true);
            }

            GeneratedOn(const GeneratedOn&) = delete;
            GeneratedOn& operator=(const GeneratedOn&) = delete;

            ~GeneratedOn() {
                NativePrograms::SetEnabled(_was);
            }

        private:
            bool _was;
        };

        template <typename T>
        Value<T> Compile(StringView text, bool fuse = true) {
            const auto compiled = CompileValue<T>(text, NativeProgramSetDeclaration(), CompileOptions{.fuse = fuse});
            EXPECT_TRUE(compiled.has_value()) << String(text).c_str() << ": " << (compiled.has_value() ? "" : compiled.error().message.c_str());
            EXPECT_TRUE(compiled.has_value() && compiled->Kind() == ValueKind::Program) << String(text).c_str();
            return compiled.has_value() ? *compiled : Value<T>{};
        }

        /// The bytes of the program's result (the rest of an image stays zero), from `Run` and from `RunTraced`, and what the sink heard
        struct Both {
            RegisterImage plain{};
            RegisterImage traced{};
            Recorder recorder;
        };

        void RunBoth(const Program& program, const Context& context, Both& both) {
            Run(program, context, both.plain.bytes.data());
            RunTraced(program, context, both.traced.bytes.data(), both.recorder);
        }

        /// The program is bound to a generated function and `Evaluate` gives the bytes the traced run gives
        template <typename T>
        void ExpectTracedLikeEvaluate(StringView text, bool fuse = true) {
            const GeneratedOn generated;
            const SetWorld world;
            const Value<T> value = Compile<T>(text, fuse);
            const Program& program = ProgramPool::Get(value.Id());
            ASSERT_NE(ProgramPool::Native(value.Id()), nullptr) << String(text).c_str() << ": no generated function is bound, the traced run would prove nothing";

            Both both;
            RunBoth(program, world.context, both);
            const T evaluated = value.Evaluate(world.context);

            EXPECT_EQ(both.traced, both.plain) << String(text).c_str();
            EXPECT_EQ(std::memcmp(&evaluated, both.traced.bytes.data(), sizeof(T)), 0) << String(text).c_str() << ": Evaluate and the traced run differ";
            EXPECT_EQ(both.recorder.steps.size(), program.Code().size()) << String(text).c_str() << ": a straight program runs every instruction once";
            ASSERT_FALSE(both.recorder.steps.empty());
            EXPECT_EQ(std::memcmp(both.recorder.steps.back().written.bytes.data(), both.traced.bytes.data(), sizeof(T)), 0) << String(text).c_str() << ": the last write is the result";
        }

    }  // namespace

    TEST(ProgramTraceRunTest, AStraightProgramReportsItsInstructionsInOrder) {
        const GeneratedOn generated;
        const SetWorld world;
        for (const bool fuse : {false, true}) {
            const Value<int32_t> value = Compile<int32_t>(kNativeIntFormulas[0], fuse);
            const Program& program = ProgramPool::Get(value.Id());
            Both both;
            RunBoth(program, world.context, both);

            ASSERT_EQ(both.recorder.steps.size(), program.Code().size()) << (fuse ? "fused" : "unfused");
            for (size_t i = 0; i < program.Code().size(); ++i) {
                const Step& step = both.recorder.steps[i];
                EXPECT_EQ(step.pc, i);
                EXPECT_EQ(step.nextPc, i + 1);
                EXPECT_EQ(step.instruction, program.Code()[i]);
                if (step.instruction.op == Opcode::LoadConst) {
                    EXPECT_EQ(step.written, program.Constants()[step.instruction.imm]) << "the register an instruction wrote holds its result at the call";
                }
            }
            int32_t result = 0;
            std::memcpy(&result, both.traced.bytes.data(), sizeof(result));
            EXPECT_EQ(result, kAttack * (100 + kLevel * 10) / 100);
            EXPECT_EQ(both.traced, both.plain);
        }
    }

    TEST(ProgramTraceRunTest, ALoadConstAloneIsOneStep) {
        const SetWorld world;
        ProgramBuilder builder;
        const uint8_t reg = Require(builder.AcquireRegister());
        builder.EmitLoadConst(reg, int32_t{41});
        const auto program = builder.Finish(ProgramType::Int32, reg);
        ASSERT_TRUE(program.has_value());

        Recorder recorder;
        int32_t result = 0;
        RunTraced(*program, world.context, &result, recorder);

        EXPECT_EQ(result, 41);
        ASSERT_EQ(recorder.steps.size(), 1U);
        EXPECT_EQ(recorder.steps[0].instruction.op, Opcode::LoadConst);
        EXPECT_EQ(recorder.steps[0].nextPc, 1U);
    }

    TEST(ProgramTraceRunTest, AFoldRunsItsBodyOncePerElementAndTheLoopEndJumpsBack) {
        const GeneratedOn generated;
        const SetWorld world;
        const Value<int32_t> value = Compile<int32_t>("sum(hand.tiles, t, t.rank)");
        const Program& program = ProgramPool::Get(value.Id());
        Both both;
        RunBoth(program, world.context, both);

        const size_t tiles = world.hand.tiles.size();
        size_t loads = 0;
        size_t begins = 0;
        Vector<Step> ends;
        for (const Step& step : both.recorder.steps) {
            loads += IsLoadElem(step.instruction.op) ? 1 : 0;
            begins += step.instruction.op == Opcode::LoopBegin ? 1 : 0;
            if (step.instruction.op == Opcode::LoopEnd) {
                ends.push_back(step);
            }
        }
        EXPECT_EQ(begins, 1U);
        EXPECT_EQ(loads, tiles) << "the body reads one field of each element";
        ASSERT_EQ(ends.size(), tiles);
        for (size_t i = 0; i < ends.size(); ++i) {
            const bool last = i + 1 == ends.size();
            EXPECT_EQ(ends[i].nextPc, last ? ends[i].pc + 1 : ends[i].instruction.imm) << "element " << i;
        }
        int32_t expected = 0;
        for (const ProgramListTile& tile : world.hand.tiles) {
            expected += tile.rank;
        }
        int32_t result = 0;
        std::memcpy(&result, both.traced.bytes.data(), sizeof(result));
        EXPECT_EQ(result, expected);
        EXPECT_EQ(both.traced, both.plain);
    }

    TEST(ProgramTraceRunTest, ALoopOverAnEmptyListJumpsOverItsBody) {
        const SetWorld world;
        const Value<int32_t> value = Compile<int32_t>("sum(hand.none, t, t.rank)");
        const Program& program = ProgramPool::Get(value.Id());
        Both both;
        RunBoth(program, world.context, both);

        bool jumped = false;
        for (const Step& step : both.recorder.steps) {
            EXPECT_FALSE(IsLoadElem(step.instruction.op)) << "no element, no body";
            if (step.instruction.op == Opcode::LoopBegin) {
                jumped = step.nextPc == step.instruction.imm;
            }
        }
        EXPECT_TRUE(jumped);
        EXPECT_EQ(both.traced, both.plain);
    }

    TEST(ProgramTraceRunTest, AnEarlyExitIsAStepToo) {
        const SetWorld world;
        const Value<bool> value = Compile<bool>("any(hand.tiles, t, t.rank > -4)");
        const Program& program = ProgramPool::Get(value.Id());
        Both both;
        RunBoth(program, world.context, both);

        size_t breaks = 0;
        for (const Step& step : both.recorder.steps) {
            if (step.instruction.op == Opcode::BreakIf && step.nextPc == step.instruction.imm) {
                ++breaks;
            }
        }
        EXPECT_EQ(breaks, 1U) << "the second tile is the first with a rank above -4";
        EXPECT_EQ(both.traced, both.plain);
    }

    TEST(ProgramTraceRunTest, AProgramBoundToAGeneratedFunctionIsTracedThroughTheInterpreter) {
        ExpectTracedLikeEvaluate<int32_t>(kNativeIntFormulas[1]);
        ExpectTracedLikeEvaluate<int32_t>(kNativeIntFormulas[0], false);
    }

    TEST(ProgramTraceRunTest, TheResultOfABoolAVec3AndARegisteredTypeIsTheBytesRunGives) {
        ExpectTracedLikeEvaluate<bool>(kNativeBoolFormulas[0]);
        ExpectTracedLikeEvaluate<Vec3>(kNativeVec3Formulas[0]);

        static_cast<void>(RegisterDomainToys());
        const GeneratedOn generated;
        const SetWorld world;
        const Value<DomainBits256> mask = Compile<DomainBits256>(kNativeDomainMaskFormulas[1]);
        Both both;
        RunBoth(ProgramPool::Get(mask.Id()), world.context, both);
        EXPECT_EQ(both.traced, both.plain);
        const DomainBits256 evaluated = mask.Evaluate(world.context);
        EXPECT_EQ(std::memcmp(&evaluated, both.traced.bytes.data(), sizeof(evaluated)), 0);
        EXPECT_FALSE(both.recorder.steps.empty());
        const Step* call = nullptr;
        for (const Step& step : both.recorder.steps) {
            call = step.instruction.op == Opcode::CallDomain ? &step : call;
        }
        EXPECT_NE(call, nullptr) << "a call of a game's operation is a step";
    }

    TEST(ProgramTraceRunTest, AnEnumResultIsItsIndex) {
        const GeneratedOn generated;
        const SetWorld world;
        const Value<ValueNarrowEnum> value = Compile<ValueNarrowEnum>(kNativeNarrowEnumFormulas[0]);
        const Program& program = ProgramPool::Get(value.Id());
        Both both;
        RunBoth(program, world.context, both);

        ProgramEnum index;
        std::memcpy(&index, both.traced.bytes.data(), sizeof(index));
        EXPECT_EQ(static_cast<ValueNarrowEnum>(index.index), value.Evaluate(world.context));
        EXPECT_EQ(both.traced, both.plain);
    }

    TEST(ProgramTraceRunTest, RunAllocatesNothingBeforeAndAfterATracedRun) {
        const SetWorld world;
        const Value<int32_t> value = Compile<int32_t>("sum(hand.tiles, t, t.rank)");
        const Program& program = ProgramPool::Get(value.Id());
        int32_t result = 0;
        Recorder recorder;
        recorder.steps.reserve(program.Code().size() * (world.hand.tiles.size() + 1));

        ExpectNoAllocations([&] { BECore::Run(program, world.context, &result); });
        RunTraced(program, world.context, &result, recorder);
        ExpectNoAllocations([&] { BECore::Run(program, world.context, &result); });
    }

}  // namespace BECore::Tests
