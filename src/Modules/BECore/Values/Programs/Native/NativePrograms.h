#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief One generated function and the program it was generated from
     *
     * The generated file holds a table of these. The function is bound to a program only when every field below equals the program's (`ProgramCodeView`), so
     * a program that changed after the file was generated runs in the interpreter, never in stale code. `codeHash` is `Program::CodeHash()` of the program: it
     * only finds the candidate. `source` is the text of the formula, for the warning of a miss. `domainFingerprint` is `ProgramDomain::Fingerprint()` of the process
     * that generated the file when the program calls an operation of a game (`CallDomain`: its id is the order of registration, `Domain/CLAUDE.md`), 0 otherwise.
     */
    struct NativeProgramEntry {
        uint64_t codeHash = 0;
        ProgramType resultType = ProgramType::Int32;
        uint8_t resultRegister = 0;
        uint8_t registerCount = 0;
        LevelMask reads = 0;
        Span<const Instruction> code;
        Span<const RegisterImage> constants;
        StringView source;
        NativeProgramFn fn = nullptr;
        uint64_t domainFingerprint = 0;

        [[nodiscard]] ProgramCodeView CodeView() const {
            return ProgramCodeView{resultType, resultRegister, registerCount, reads, code, constants};
        }
    };

    /// What `NativePrograms::Counts` reports
    struct NativeProgramCounts {
        /// Programs of the pool that run a generated function
        size_t bound = 0;
        /// Programs of the pool that run in the interpreter although generated code is on
        size_t missed = 0;
    };

    /**
     * @brief The generated functions of this process and their binding to the programs of the `ProgramPool`
     *
     * A generated file (`ProgramCodegen`) defines a `NativeProgramRegistrar` of its entries; the registrar calls `Install` while the process starts. `Value::Evaluate`
     * then runs the bound function instead of `Run`. A program is bound when it is interned (the pool looks it up) and, for the programs already in the pool, when
     * `Install` runs, so the order of loading and of installing does not matter. `Run(program, ...)` never calls native code: it is the reference the tests compare with.
     * Thread-safe; the pool's lock is taken before the registry's, never the other way round.
     */
    class NativePrograms {
    public:
        /**
         * @brief Adds @p entries and binds the programs already in the pool that match one
         *
         * May be called more than once (the engine's set and a game's). The entries must outlive the process: the registry keeps pointers into them. An entry with a
         * `domainFingerprint` is bound only while `ProgramDomain::Fingerprint()` equals it: a generated file installs itself while the process starts, before the game
         * registered its types, so such an entry waits for `BindPending`.
         */
        static void Install(Span<const NativeProgramEntry> entries);

        /**
         * @brief Binds the waiting entries whose `domainFingerprint` equals the process's now
         *
         * `ApplyNativeProgramsFlags` calls it, so a game registers its types and operations before that call. The entries that still wait are one `LOG_WARNING` when
         * `SetWarnOnMiss` asked for it: the process registered another set, or the same set in another order, and those programs run in the interpreter. A bound entry
         * stays bound (the registry only appends).
         */
        static void BindPending();

        /// Off: every program runs in the interpreter (for a run that compares the two and the `--interpreted` flag); on: the bound functions come back. On at start
        static void SetEnabled(bool enabled);

        [[nodiscard]] static bool IsEnabled();

        /**
         * @brief Whether a program that no entry matches is one warning
         *
         * Off at start: the engine's test executable links a generated set that covers only some of its programs. A tool that asked for generated code turns it on.
         */
        static void SetWarnOnMiss(bool warn);

        [[nodiscard]] static NativeProgramCounts Counts();

        /// Whether a generated set is installed at all: a program with no function is a stale file when it is, and the normal case when it is not
        [[nodiscard]] static bool HasGeneratedCode();

        /// One `LOG_INFO` line, `Native programs: N bound, M missed` (`Counts`)
        static void LogSummary();

    private:
        friend class ProgramPool;

        /// The function of the entry that equals @p program, or null (also while disabled); the pool calls it under its lock
        [[nodiscard]] static NativeProgramFn Find(const Program& program);

        /// One `LOG_WARNING` naming the formula when generated code is on and the warning is asked for
        static void ReportMiss(PoolString source);
    };

    /// A static object of a generated file: its constructor installs the file's entries
    class NativeProgramRegistrar {
    public:
        explicit NativeProgramRegistrar(Span<const NativeProgramEntry> entries) {
            NativePrograms::Install(entries);
        }
    };

}  // namespace BECore
