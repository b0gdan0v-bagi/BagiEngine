// `NativePrograms::Install` binds an entry that names a registry fingerprint (`NativeProgramEntry::domainFingerprint`) only while the process's `ProgramDomain::Fingerprint()`
// equals it: the operation id of a `CallDomain` is the order of registration, so a function generated under another registry would call another operation. The cases hand
// `Install` entries of their own over programs of one constant, which no set of the build holds.

#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Native/NativePrograms.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/LogRecorder.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr int32_t kFirstConstant = 4'100'000;

        void FunctionOne(const Context&, void*) {}

        /// A program no formula and no other test makes: a constant of its own on every call, so a run repeated in one process (`--gtest_repeat`) meets a program that is not bound yet
        ProgramId FreshProgram() {
            static int32_t next = kFirstConstant;
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            builder.EmitLoadConst(reg, next++);
            auto program = builder.Finish(ProgramType::Int32, reg, PoolString{});
            EXPECT_TRUE(program.has_value());
            const Optional<ProgramId> id = ProgramPool::Intern(std::move(*Require(&program)));
            return Require(id);
        }

        /// An entry for @p id that stays for the life of the process, as `Install` needs
        const NativeProgramEntry& KeepEntry(ProgramId id, uint64_t fingerprint) {
            static Vector<UniquePtr<NativeProgramEntry>> kept;
            const Program& program = ProgramPool::Get(id);
            auto entry = MakeUnique<NativeProgramEntry>();
            entry->codeHash = program.CodeHash();
            entry->resultType = program.ResultType();
            entry->resultRegister = program.ResultRegister();
            entry->registerCount = program.RegisterCount();
            entry->reads = program.Reads();
            entry->code = program.Code();
            entry->constants = program.Constants();
            entry->fn = &FunctionOne;
            entry->domainFingerprint = fingerprint;
            kept.push_back(std::move(entry));
            return *kept.back();
        }

        struct WarnOnMissGuard {
            WarnOnMissGuard() {
                NativePrograms::SetWarnOnMiss(true);
            }
            WarnOnMissGuard(const WarnOnMissGuard&) = delete;
            WarnOnMissGuard& operator=(const WarnOnMissGuard&) = delete;
            ~WarnOnMissGuard() {
                NativePrograms::SetWarnOnMiss(false);
            }
        };

    }  // namespace

    TEST(NativeProgramFingerprintTest, AnEntryOfAnotherRegistryBindsNothingAndWarnsOnce) {
        const ProgramId id = FreshProgram();
        const NativeProgramEntry& entry = KeepEntry(id, ProgramDomain::Fingerprint() + 1);

        const WarnOnMissGuard warn;
        LogRecorder recorder;
        NativePrograms::Install(Span<const NativeProgramEntry>(&entry, 1));
        NativePrograms::BindPending();
        NativePrograms::BindPending();
        LogEvent::Flush();

        EXPECT_EQ(ProgramPool::Native(id), nullptr);
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"another set of registered types and operations"}), 1U);
    }

    TEST(NativeProgramFingerprintTest, AnEntryOfThisRegistryBindsWhenInstalledAndOneWithNoFingerprintAlways) {
        const ProgramId first = FreshProgram();
        const ProgramId second = FreshProgram();
        const NativeProgramEntry& same = KeepEntry(first, ProgramDomain::Fingerprint());
        const NativeProgramEntry& independent = KeepEntry(second, 0);

        NativePrograms::Install(Span<const NativeProgramEntry>(&same, 1));
        NativePrograms::Install(Span<const NativeProgramEntry>(&independent, 1));

        EXPECT_EQ(ProgramPool::Native(first), &FunctionOne);
        EXPECT_EQ(ProgramPool::Native(second), &FunctionOne);
    }

}  // namespace BECore::Tests
