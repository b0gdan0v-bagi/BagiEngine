#include "EffectProgramBuilder.h"

#include <BECore/Values/Programs/Domain/ProgramDomain.h>

namespace BECore {

    namespace {

        [[nodiscard]] bool IsFieldWidthValid(ProgramType type, uint8_t bytes) {
            switch (type) {
                case ProgramType::Enum:
                    return IsEnumFieldBytes(bytes);
                case ProgramType::Domain:
                    return bytes >= 1 && bytes <= kProgramRegisterBytes;
                default:
                    return bytes == ProgramTypeSize(type);
            }
        }

        struct StepChecker {
            size_t index;
            const EffectStep& step;
            LevelMask reads = 0;

            [[nodiscard]] std::unexpected<ProgramError> Refuse(StringView what) const {
                return std::unexpected(ProgramError{Format("step {} ({}): {}", index, EnumUtils<EffectOp>::ToString(step.op), what), 0});
            }

            /// Checks the operand against the type the step needs of it and adds the levels it reads
            [[nodiscard]] Optional<String> Operand(const EffectOperand& operand, ProgramType expected, StringView role) {
                if (operand.type != expected) {
                    return Format("{} is a {}, the step needs a {}", role, EnumUtils<ProgramType>::ToString(operand.type), EnumUtils<ProgramType>::ToString(expected));
                }
                switch (operand.kind) {
                    case CompiledKind::Constant:
                        return std::nullopt;
                    case CompiledKind::Slot:
                        if (operand.slot.level >= kMaxContextLevels) {
                            return Format("{} reads level {}, a context has {}", role, operand.slot.level, kMaxContextLevels);
                        }
                        if (!IsFieldWidthValid(operand.type, operand.bytes)) {
                            return Format("{} reads a field of {} bytes, which is no width of a {}", role, operand.bytes, EnumUtils<ProgramType>::ToString(operand.type));
                        }
                        reads |= LevelBit(operand.slot.level);
                        return std::nullopt;
                    case CompiledKind::Program:
                        if (operand.program >= ProgramPool::Count()) {
                            return Format("{} is program {}, which is not in the pool", role, operand.program);
                        }
                        if (ProgramPool::Get(operand.program).ResultType() != operand.type) {
                            return Format("{} is a program that computes a {}, not a {}", role, EnumUtils<ProgramType>::ToString(ProgramPool::Get(operand.program).ResultType()),
                                          EnumUtils<ProgramType>::ToString(operand.type));
                        }
                        reads |= ProgramPool::Get(operand.program).Reads();
                        return std::nullopt;
                }
                return String("an operand of a kind that does not exist");
            }
        };

    }  // namespace

    uint32_t EffectProgramBuilder::AddStore(SlotRef target, ProgramType type, uint8_t bytes, const EffectOperand& value) {
        EffectStep step;
        step.op = EffectOp::Store;
        step.target = target;
        step.type = type;
        step.bytes = EffectFieldBytes(type, bytes);
        step.operands[0] = value;
        _steps.push_back(step);
        return NextStep() - 1;
    }

    uint32_t EffectProgramBuilder::AddJumpIfNot(const EffectOperand& condition) {
        EffectStep step;
        step.op = EffectOp::JumpIfNot;
        step.operands[0] = condition;
        _steps.push_back(step);
        return NextStep() - 1;
    }

    uint32_t EffectProgramBuilder::AddJump() {
        EffectStep step;
        step.op = EffectOp::Jump;
        _steps.push_back(step);
        return NextStep() - 1;
    }

    uint32_t EffectProgramBuilder::AddEmit(CommandId command, Span<const EffectOperand> arguments) {
        EffectStep step;
        step.op = EffectOp::Emit;
        step.command = command;
        // More than the most is kept as it is, so that `Finish` names the count that does not fit the command
        step.count = static_cast<uint8_t>(std::min<size_t>(arguments.size(), std::numeric_limits<uint8_t>::max()));
        for (size_t i = 0; i < std::min<size_t>(arguments.size(), kMaxCommandArguments); ++i) {
            step.operands[i] = arguments[i];
        }
        _steps.push_back(step);
        return NextStep() - 1;
    }

    void EffectProgramBuilder::PatchJump(uint32_t step, uint32_t target) {
        ASSERT(step < _steps.size() && (_steps[step].op == EffectOp::Jump || _steps[step].op == EffectOp::JumpIfNot), "EffectProgramBuilder: only a jump is patched");
        if (step < _steps.size()) {
            _steps[step].jump = target;
        }
    }

    std::expected<IntrusivePtr<const EffectProgram>, ProgramError> EffectProgramBuilder::Finish(String source) const {
        if (_steps.size() > kMaxEffectSteps) {
            return std::unexpected(ProgramError{Format("the script has {} steps, the most is {}: split it", _steps.size(), kMaxEffectSteps), 0});
        }
        LevelMask reads = 0;
        LevelMask writes = 0;
        for (size_t i = 0; i < _steps.size(); ++i) {
            const EffectStep& step = _steps[i];
            StepChecker check{i, step};
            Optional<String> problem;
            switch (step.op) {
                case EffectOp::Store:
                    if (step.target.level >= kMaxContextLevels) {
                        return check.Refuse(Format("stores to level {}, a context has {}", step.target.level, kMaxContextLevels));
                    }
                    if (!IsFieldWidthValid(step.type, step.bytes)) {
                        return check.Refuse(Format("stores {} bytes, which is no width of a {}", step.bytes, EnumUtils<ProgramType>::ToString(step.type)));
                    }
                    problem = check.Operand(step.operands[0], step.type, "the value");
                    writes |= LevelBit(step.target.level);
                    break;
                case EffectOp::JumpIfNot:
                    problem = check.Operand(step.operands[0], ProgramType::Bool, "the condition");
                    [[fallthrough]];
                case EffectOp::Jump:
                    if (step.jump == kUnpatchedJump) {
                        return check.Refuse("the jump was never given a target");
                    }
                    if (step.jump <= i) {
                        return check.Refuse(Format("jumps back to step {}: a jump goes forward", step.jump));
                    }
                    if (step.jump > _steps.size()) {
                        return check.Refuse(Format("jumps to step {}, the script has {}", step.jump, _steps.size()));
                    }
                    break;
                case EffectOp::Emit: {
                    if (step.command >= EffectCommands::Count()) {
                        return check.Refuse(Format("emits command {}, which is not registered", step.command));
                    }
                    const EffectCommand command = EffectCommands::Command(step.command);
                    if (step.count != command.parameterCount) {
                        return check.Refuse(Format("emits '{}' with {} arguments, it takes {}", command.name, step.count, command.parameterCount));
                    }
                    for (size_t a = 0; a < step.count && !problem; ++a) {
                        problem = check.Operand(step.operands[a], command.parameters[a].type, Format("argument {} of '{}'", a + 1, command.name));
                    }
                    break;
                }
            }
            if (problem) {
                return check.Refuse(*problem);
            }
            reads |= check.reads;
        }
        auto program = New<EffectProgram>(PassKey<EffectProgramBuilder>{});
        program->_steps = _steps;
        program->_reads = reads;
        program->_writes = writes;
        program->_source = std::move(source);
        return IntrusivePtr<const EffectProgram>(std::move(program));
    }

}  // namespace BECore
