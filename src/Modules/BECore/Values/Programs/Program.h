#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    class NativePrograms;

    /// A function generated from a program (`Native/NativePrograms.h`): it reads the context like `Run` and writes the result, `ProgramTypeSize(ResultType())` bytes, to the second argument
    using NativeProgramFn = void (*)(const Context&, void* result);

    /// A refused program or formula: what was expected and found, and where in the text (1-based byte; 0 when no text is involved)
    struct ProgramError {
        String message;
        uint32_t column = 0;
    };

    /**
     * @brief What makes two programs "the same code": the result, the registers, the levels read, the instructions and the constants (never the source text)
     *
     * The one definition of the comparison, used by the pool to intern and by `NativePrograms` to bind a generated function, so the two cannot disagree. A view
     * borrows: it is valid while the program or the table it came from is.
     */
    struct ProgramCodeView {
        ProgramType resultType = ProgramType::Int32;
        uint8_t resultRegister = 0;
        uint8_t registerCount = 0;
        LevelMask reads = 0;
        Span<const Instruction> code;
        Span<const RegisterImage> constants;

        [[nodiscard]] bool operator==(const ProgramCodeView& other) const;
    };

    /**
     * @brief A compiled formula: typed code over registers, straight-line except for the loops over lists (`Lists/`)
     *
     * Built by `ProgramBuilder` and kept in the `ProgramPool`; immutable after that. A program is pure and total: it reads the context and writes
     * nothing, and `Run` allocates nothing (`Interpreter.h`). Two programs with equal code are one program to the pool whatever text they came from.
     */
    class Program {
    public:
        [[nodiscard]] ProgramType ResultType() const {
            return _resultType;
        }

        /// The register that holds the result when the code has run
        [[nodiscard]] uint8_t ResultRegister() const {
            return _resultRegister;
        }

        /// The registers the code uses (the highest one the builder gave out, plus one)
        [[nodiscard]] uint8_t RegisterCount() const {
            return _registerCount;
        }

        /// The levels of the context the code reads (what `Context::VersionKey` is asked about)
        [[nodiscard]] LevelMask Reads() const {
            return _reads;
        }

        [[nodiscard]] Span<const Instruction> Code() const {
            return Span<const Instruction>(_code.data(), _code.size());
        }

        [[nodiscard]] Span<const RegisterImage> Constants() const {
            return Span<const RegisterImage>(_constants.data(), _constants.size());
        }

        /// The text the program came from, of the first formula the pool saw with this code; empty for a program assembled by hand
        [[nodiscard]] PoolString Source() const {
            return _source;
        }

        /// The fields `SameCode` compares
        [[nodiscard]] ProgramCodeView CodeView() const {
            return ProgramCodeView{_resultType, _resultRegister, _registerCount, _reads, Code(), Constants()};
        }

        /// Whether the code, the constants and the result are the same: the source text is not part of it
        [[nodiscard]] bool SameCode(const Program& other) const;

        /// A hash of what `SameCode` compares
        [[nodiscard]] uint64_t CodeHash() const;

        /// One instruction per line with its registers, for a failing test and a debugger
        [[nodiscard]] String Disassemble() const;

    private:
        friend class ProgramBuilder;

        ProgramType _resultType = ProgramType::Int32;
        uint8_t _resultRegister = 0;
        uint8_t _registerCount = 0;
        LevelMask _reads = 0;
        Vector<Instruction> _code;
        Vector<RegisterImage> _constants;
        PoolString _source;
    };

    /// The place of a program in the pool: what a `Value` of the kind `Program` holds
    using ProgramId = uint32_t;

    /**
     * @brief The process-wide, append-only table of programs
     *
     * Like `PoolString` it never shrinks and an entry never moves, so a `Value` keeps only the id and `Get` is a lookup with no lock. One entry per
     * distinct program: loading the same content again adds nothing. Thread-safe.
     */
    class ProgramPool {
    public:
        /// The most programs the pool holds; a hostile content file cannot make it grow without bound
        static constexpr size_t kMaxPrograms = 1U << 18;

        /// The id of the program in the pool that has the same code as @p program (the pool takes it when it has none); `nullopt` when the pool is full
        [[nodiscard]] static Optional<ProgramId> Intern(Program program);

        /// The program of an id `Intern` gave out; its address is stable for the life of the process
        [[nodiscard]] static const Program& Get(ProgramId id);

        [[nodiscard]] static size_t Count();

        /**
         * @brief The generated function bound to the program of @p id, or null (`Native/NativePrograms.h`)
         *
         * One atomic load, no lock. Null while no generated set holds the program and after `NativePrograms::SetEnabled(false)`; @p id must be one `Intern` gave out.
         */
        [[nodiscard]] static NativeProgramFn Native(ProgramId id);

        /// How many programs have a bound function
        [[nodiscard]] static size_t NativeCount();

    private:
        friend class NativePrograms;

        /// Binds every program to what `NativePrograms::Find` says now (null for all of them while disabled); the one place a binding changes after `Intern`
        static void RebindNative();
    };

}  // namespace BECore
