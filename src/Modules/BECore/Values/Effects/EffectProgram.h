#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/PassKey.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectCommands.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace BECore {

    class EffectProgramBuilder;

    /// The most steps a script has: a load error beyond it, so a hostile text cannot make a script that is long to run or to keep
    inline constexpr size_t kMaxEffectSteps = 512;

    /// The width of a field of @p type that a store writes and a slot operand reads: an enum's underlying width and a registered type's size are not in the type (@p bytes says), the others' are
    [[nodiscard]] constexpr uint8_t EffectFieldBytes(ProgramType type, uint8_t bytes) {
        return type == ProgramType::Enum || type == ProgramType::Domain ? bytes : static_cast<uint8_t>(ProgramTypeSize(type));
    }

    /**
     * @brief What a step reads: the same three forms as a compiled formula
     *
     * A constant is the register image of a value of `type`; a slot is one field of a level (`bytes` its width, for an enum the underlying type's and for a registered type its size);
     * a program is an id of the `ProgramPool` that `RunEffects` evaluates with the generated function when one is bound and with the interpreter otherwise.
     */
    struct EffectOperand {
        CompiledKind kind = CompiledKind::Constant;
        ProgramType type = ProgramType::Int32;
        uint8_t bytes = 0;
        ProgramId program = 0;
        SlotRef slot{};
        RegisterImage constant{};

        [[nodiscard]] static EffectOperand OfConstant(ProgramType type, const RegisterImage& image) {
            EffectOperand operand;
            operand.kind = CompiledKind::Constant;
            operand.type = type;
            operand.constant = image;
            return operand;
        }

        /// @p bytes only for an enum (its field's width) and a registered type (its size); the other types have theirs
        [[nodiscard]] static EffectOperand OfSlot(ProgramType type, SlotRef slot, uint8_t bytes = 0) {
            EffectOperand operand;
            operand.kind = CompiledKind::Slot;
            operand.type = type;
            operand.slot = slot;
            operand.bytes = EffectFieldBytes(type, bytes);
            return operand;
        }

        /// What `CompileExpression` made of a text. @p bytes as in `OfSlot`
        [[nodiscard]] static EffectOperand Of(const CompiledExpression& compiled, uint8_t bytes = 0) {
            switch (compiled.kind) {
                case CompiledKind::Constant:
                    return OfConstant(compiled.type, compiled.constant);
                case CompiledKind::Slot:
                    return OfSlot(compiled.type, compiled.slot, bytes);
                case CompiledKind::Program:
                    break;
            }
            return OfProgram(compiled.type, compiled.program);
        }

        [[nodiscard]] static EffectOperand OfProgram(ProgramType type, ProgramId program) {
            EffectOperand operand;
            operand.kind = CompiledKind::Program;
            operand.type = type;
            operand.program = program;
            return operand;
        }
    };

    inline constexpr uint32_t kUnpatchedJump = std::numeric_limits<uint32_t>::max();

    CORE_ENUM(EffectOp, uint8_t, Store, JumpIfNot, Jump, Emit)

    /**
     * @brief One statement of a script, flat so that a run is one `switch` over an array
     *
     * `Store`: `operands[0]` into the field `target` of `type` (`bytes` wide). `JumpIfNot`: when the `Bool` in `operands[0]` is false, go to step `jump`. `Jump`: go to step `jump`.
     * `Emit`: push command `command` with the first `count` operands as its arguments. A jump goes forward only, to a later step or to the end (`jump == Steps().size()`).
     */
    struct EffectStep {
        EffectOp op = EffectOp::Jump;
        ProgramType type = ProgramType::Int32;
        uint8_t bytes = 0;
        uint8_t count = 0;
        uint32_t jump = kUnpatchedJump;
        CommandId command = kNoCommand;
        SlotRef target{};
        Array<EffectOperand, kMaxCommandArguments> operands{};
    };

    /**
     * @brief A script: steps run top to bottom with forward jumps, so a run always ends
     *
     * Built by `EffectProgramBuilder`, immutable after that, shared through `IntrusivePtr<const EffectProgram>` (a content field holds the handle). `RunEffects` runs it
     * (`EffectRunner.h`); the programs inside its operands are the pure formulas of the `ProgramPool`.
     */
    class EffectProgram : public RefCounted {
    public:
        explicit EffectProgram(PassKey<EffectProgramBuilder> /*key*/) {}

        [[nodiscard]] Span<const EffectStep> Steps() const {
            return Span<const EffectStep>(_steps.data(), _steps.size());
        }

        /// The levels the operands and the conditions read (`Context::VersionKey` of them tells whether a result of the script can have changed)
        [[nodiscard]] LevelMask Reads() const {
            return _reads;
        }

        /// The levels the script stores to: each must be `Context::SetMutable` when it runs
        [[nodiscard]] LevelMask Writes() const {
            return _writes;
        }

        /// The text the script came from; empty for a script assembled by hand
        [[nodiscard]] const String& Source() const {
            return _source;
        }

        /// One step per line, for a failing test and a debugger
        [[nodiscard]] String Disassemble() const;

    private:
        friend class EffectProgramBuilder;

        Vector<EffectStep> _steps;
        LevelMask _reads = 0;
        LevelMask _writes = 0;
        String _source;
    };

}  // namespace BECore
