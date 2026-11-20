// The registry of generated functions (Values/Programs/Native/NativePrograms.h): when a function binds to a program, that a stale one never does, and that
// `Value::Evaluate` is the only caller. The functions here are hand-written and return a marker, so a test sees which path ran.

#include <BECore/Logger/LogEvent.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/LogRecorder.h>
#include <Common/Require.h>
#include <atomic>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr int32_t kMarker = -424242;

        void MarkerFunction(const Context& /*context*/, void* result) {
            constexpr int32_t kValue = kMarker;
            std::memcpy(result, &kValue, sizeof(kValue));
        }

        /// An entry and the code it owns, kept for the life of the process: the registry holds pointers into them and has no way to forget an entry
        struct HeldEntry {
            Vector<Instruction> code;
            Vector<RegisterImage> constants;
            NativeProgramEntry entry;
        };

        /// The entry a generator would write for @p program; with @p changeCode the code differs in one instruction while the hash stays the program's
        const NativeProgramEntry& MakeEntry(const Program& program, bool changeCode = false) {
            static Vector<UniquePtr<HeldEntry>> held;
            auto entry = MakeUnique<HeldEntry>();
            entry->code.assign(program.Code().begin(), program.Code().end());
            entry->constants.assign(program.Constants().begin(), program.Constants().end());
            if (changeCode) {
                ++entry->code.front().imm;
            }
            entry->entry = NativeProgramEntry{program.CodeHash(),
                                              program.ResultType(),
                                              program.ResultRegister(),
                                              program.RegisterCount(),
                                              program.Reads(),
                                              Span<const Instruction>(entry->code.data(), entry->code.size()),
                                              Span<const RegisterImage>(entry->constants.data(), entry->constants.size()),
                                              program.Source().ToStringView(),
                                              &MarkerFunction};
            held.push_back(std::move(entry));
            return held.back()->entry;
        }

        void Install(const NativeProgramEntry& entry) {
            NativePrograms::Install(Span<const NativeProgramEntry>(&entry, 1));
        }

        /// Restores what a test changed in the process-wide registry
        class RegistryGuard {
        public:
            RegistryGuard() = default;
            RegistryGuard(const RegistryGuard&) = delete;
            RegistryGuard& operator=(const RegistryGuard&) = delete;

            ~RegistryGuard() {
                NativePrograms::SetEnabled(true);
                NativePrograms::SetWarnOnMiss(false);
            }
        };

        Optional<Value<int32_t>> CompileProgram(const ValueWorld& world, StringView text) {
            const auto value = CompileValue<int32_t>(text, world.declaration);
            if (!value.has_value() || value->Kind() != ValueKind::Program) {
                return std::nullopt;
            }
            return *value;
        }

        /// Every formula of a test has constants no other test uses, so the entries it installs never bind a program of another one
        String Formula(int32_t factor, int32_t addend) {
            return Format("caster.attack * {} + {}", factor, addend);
        }

        int32_t Reference(const ValueWorld& world, const Value<int32_t>& value) {
            return Run<int32_t>(ProgramPool::Get(value.Id()), world.context);
        }

    }  // namespace

    TEST(NativeProgramsTest, ABoundFunctionIsWhatEvaluateRuns) {
        const RegistryGuard guard;
        const ValueWorld world;
        const auto value = CompileProgram(world, Formula(7919, 1));
        ASSERT_TRUE(value.has_value());
        EXPECT_EQ(value->Evaluate(world.context), world.caster.attack * 7919 + 1);
        EXPECT_EQ(ProgramPool::Native(value->Id()), nullptr);

        Install(MakeEntry(ProgramPool::Get(value->Id())));

        EXPECT_EQ(ProgramPool::Native(value->Id()), &MarkerFunction);
        EXPECT_EQ(value->Evaluate(world.context), kMarker);
        EXPECT_EQ(Reference(world, *value), world.caster.attack * 7919 + 1) << "Run(program) never calls native code";
    }

    TEST(NativeProgramsTest, AProgramInternedAfterTheInstallIsBoundWhenInterned) {
        const RegistryGuard guard;
        const ValueWorld world;
        constexpr int32_t kConstant = 7927;
        ProgramBuilder builder;
        const uint8_t reg = Require(builder.AcquireRegister());
        builder.EmitLoadConst(reg, kConstant);
        auto program = builder.Finish(ProgramType::Int32, reg);
        ASSERT_TRUE(program.has_value());
        Install(MakeEntry(*program));

        const Optional<ProgramId> id = ProgramPool::Intern(std::move(*program));

        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(ProgramPool::Native(*id), &MarkerFunction);
        EXPECT_EQ(Run<int32_t>(*id, world.context), kMarker);
        EXPECT_EQ(Run<int32_t>(ProgramPool::Get(*id), world.context), kConstant);
    }

    TEST(NativeProgramsTest, AnEntryWhoseCodeDiffersInOneInstructionNeverBinds) {
        const RegistryGuard guard;
        const ValueWorld world;
        const auto value = CompileProgram(world, Formula(7933, 3));
        ASSERT_TRUE(value.has_value());

        Install(MakeEntry(ProgramPool::Get(value->Id()), true));

        EXPECT_EQ(ProgramPool::Native(value->Id()), nullptr);
        EXPECT_EQ(value->Evaluate(world.context), world.caster.attack * 7933 + 3);
    }

    TEST(NativeProgramsTest, DisablingRunsTheInterpreterAndEnablingTheFunctionAgain) {
        const RegistryGuard guard;
        const ValueWorld world;
        const auto value = CompileProgram(world, Formula(7937, 4));
        ASSERT_TRUE(value.has_value());
        Install(MakeEntry(ProgramPool::Get(value->Id())));
        ASSERT_EQ(value->Evaluate(world.context), kMarker);
        ASSERT_GE(NativePrograms::Counts().bound, 1U);

        NativePrograms::SetEnabled(false);
        EXPECT_FALSE(NativePrograms::IsEnabled());
        EXPECT_EQ(value->Evaluate(world.context), world.caster.attack * 7937 + 4);
        EXPECT_EQ(NativePrograms::Counts().bound, 0U);
        EXPECT_EQ(NativePrograms::Counts().missed, 0U) << "nothing is missed while generated code is off";

        NativePrograms::SetEnabled(true);
        EXPECT_EQ(value->Evaluate(world.context), kMarker);
        EXPECT_GE(NativePrograms::Counts().bound, 1U);
    }

    TEST(NativeProgramsTest, AMissWarnsOnceWithTheFormulaWhenAskedAndNeverWhenNot) {
        const RegistryGuard guard;
        const ValueWorld world;
        const String asked = Formula(7949, 5);
        const String notAsked = Formula(7951, 6);
        LogRecorder recorder;

        const auto quiet = CompileProgram(world, notAsked);
        LogEvent::Flush();
        NativePrograms::SetWarnOnMiss(true);
        const auto first = CompileProgram(world, asked);
        const auto again = CompileProgram(world, asked);
        LogEvent::Flush();

        ASSERT_TRUE(quiet.has_value() && first.has_value() && again.has_value());
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {StringView(asked), "stale or missing"}), 1U);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {StringView(notAsked)}), 0U);
    }

    TEST(NativeProgramsTest, InterningAndEvaluatingWhileInstallsRunGivesTheInterpretersValueOrTheFunctions) {
        const RegistryGuard guard;
        constexpr int32_t kFactor = 8009;
        constexpr int32_t kRounds = 150;
        constexpr int32_t kWorkers = 2;
        const ValueWorld world;
        std::atomic<int32_t> wrong{0};

        const auto work = [&] {
            for (int32_t i = 0; i < kRounds; ++i) {
                const auto value = CompileProgram(world, Formula(kFactor, i));
                if (!value.has_value()) {
                    wrong.fetch_add(1);
                    continue;
                }
                const int32_t result = value->Evaluate(world.context);
                if (result != world.caster.attack * kFactor + i && result != kMarker) {
                    wrong.fetch_add(1);
                }
            }
        };
        Vector<Thread> workers;
        for (int32_t worker = 0; worker < kWorkers; ++worker) {
            workers.emplace_back("Native programs test", work);
        }
        for (int32_t i = 0; i < kRounds; ++i) {
            const auto value = CompileProgram(world, Formula(kFactor, i));
            ASSERT_TRUE(value.has_value());
            Install(MakeEntry(ProgramPool::Get(value->Id())));
        }
        for (Thread& worker : workers) {
            worker.join();
        }

        EXPECT_EQ(wrong.load(), 0);
        const auto last = CompileProgram(world, Formula(kFactor, kRounds - 1));
        ASSERT_TRUE(last.has_value());
        EXPECT_EQ(last->Evaluate(world.context), kMarker);
    }

}  // namespace BECore::Tests
