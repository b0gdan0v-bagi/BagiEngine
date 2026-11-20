// The registry of generated functions (Values/Programs/Native/NativePrograms.h): when a function binds to a program, that a stale one never does, and that
// `Value::Evaluate` is the only caller. The functions here are hand-written and return a marker, so a test sees which path ran.

#include <BECore/Application/CommandLine.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Native/NativeProgramsFlags.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <BECore/Values/Value.h>
#include <Common/LogRecorder.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
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

        CommandLine MakeLine(std::initializer_list<const char*> arguments) {
            Vector<String> texts;
            for (const char* argument : arguments) {
                texts.emplace_back(argument);
            }
            CommandLine line;
            line.Set(texts);
            return line;
        }

        /// Removes the file of a test when the test ends
        struct TempFile {
            std::filesystem::path path = TestTempPath("native_programs_flags");

            ~TempFile() {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }

            String Name() const {
                return FromStdString(path.generic_string());
            }
        };

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

    TEST(NativeProgramsFlagsTest, EmitWritesTheSourceOfThePoolAndOfChosenProgramsAndLeavesAnUnchangedFileAlone) {
        const ValueWorld world;
        const auto value = CompileProgram(world, Formula(8101, 17));
        ASSERT_TRUE(value.has_value());
        const NativeCodegenOptions options{"flags test"};
        const TempFile file;
        const CommandLine line = MakeLine({"--unrelated", Format("{}={}", kEmitNativeProgramsFlag, file.Name()).c_str()});
        const String hash = Format("{:016x}", ProgramPool::Get(value->Id()).CodeHash());

        const auto whole = EmitNativeProgramsIfAsked(line, options);

        ASSERT_TRUE(whole.has_value());
        EXPECT_EQ(whole->exitCode, 0);
        EXPECT_NE(whole->message.find(file.Name()), String::npos) << "the message names the file";
        EXPECT_NE(ReadFile(file.path).find(Format("Program_{}", hash)), String::npos) << "every program of the pool is in the file";

        const ProgramId chosen = value->Id();
        const auto only = EmitNativeProgramsIfAsked(line, options, Span<const ProgramId>(&chosen, 1));
        ASSERT_TRUE(only.has_value());
        EXPECT_EQ(only->exitCode, 0);
        EXPECT_EQ(ReadFile(file.path), GenerateNativeSource(Span<const ProgramId>(&chosen, 1), options));

        constexpr auto kLongAgo = std::chrono::hours(24);
        const auto old = std::filesystem::file_time_type::clock::now() - kLongAgo;
        std::filesystem::last_write_time(file.path, old);
        ASSERT_TRUE(EmitNativeProgramsIfAsked(line, options, Span<const ProgramId>(&chosen, 1)).has_value());
        EXPECT_EQ(std::filesystem::last_write_time(file.path), old) << "a file that holds the text is not rewritten";
    }

    TEST(NativeProgramsFlagsTest, TheEmitFlagWithoutAFileIsAnErrorAndNoFlagMeansTheProgramGoesOn) {
        const NativeCodegenOptions options{"flags test"};
        const TempFile file;

        EXPECT_FALSE(EmitNativeProgramsIfAsked(MakeLine({"--interpreted"}), options).has_value());
        for (const CommandLine& line : {MakeLine({"--emit-native-programs"}), MakeLine({"--emit-native-programs="})}) {
            const auto result = EmitNativeProgramsIfAsked(line, options);
            ASSERT_TRUE(result.has_value());
            EXPECT_NE(result->exitCode, 0);
            EXPECT_NE(result->message.find("--emit-native-programs"), String::npos);
        }

        const CommandLine unwritable = MakeLine({"--emit-native-programs=/nonexistent-folder-of-native-programs/file.cpp"});
        const auto failed = EmitNativeProgramsIfAsked(unwritable, options);
        ASSERT_TRUE(failed.has_value());
        EXPECT_NE(failed->exitCode, 0);
        EXPECT_NE(failed->message.find("nonexistent-folder-of-native-programs"), String::npos);
        EXPECT_FALSE(std::filesystem::exists(file.path));
    }

    TEST(NativeProgramsFlagsTest, InterpretedRunsTheInterpreterAndAMissWarnsWhenGeneratedCodeIsLinked) {
        const RegistryGuard guard;
        const ValueWorld world;
        const auto bound = CompileProgram(world, Formula(8111, 3));
        ASSERT_TRUE(bound.has_value());
        Install(MakeEntry(ProgramPool::Get(bound->Id())));
        ASSERT_EQ(bound->Evaluate(world.context), kMarker);
        LogRecorder recorder;

        ApplyNativeProgramsFlags(MakeLine({"--interpreted"}));

        EXPECT_EQ(bound->Evaluate(world.context), world.caster.attack * 8111 + 3);
        NativePrograms::SetEnabled(true);
        ApplyNativeProgramsFlags(MakeLine({}));
        const String missed = Formula(8117, 4);
        const auto miss = CompileProgram(world, missed);
        NativePrograms::LogSummary();
        LogEvent::Flush();

        ASSERT_TRUE(miss.has_value());
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {StringView(missed), "stale or missing"}), 1U) << "the flags turned the warning on";
        EXPECT_EQ(recorder.Count(LogLevel::Info, {"Native programs:", " bound, ", " missed"}), 1U);
    }

}  // namespace BECore::Tests
