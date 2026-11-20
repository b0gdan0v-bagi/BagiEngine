#include "EffectProgram.h"

namespace BECore {

    namespace {

        [[nodiscard]] String DescribeOperand(const EffectOperand& operand) {
            const StringView type = EnumUtils<ProgramType>::ToString(operand.type);
            switch (operand.kind) {
                case CompiledKind::Constant:
                    return Format("constant {}", type);
                case CompiledKind::Slot:
                    return Format("{} at level {} offset {} ({} bytes)", type, operand.slot.level, operand.slot.offset, operand.bytes);
                case CompiledKind::Program:
                    return Format("{} program {} '{}'", type, operand.program, operand.program < ProgramPool::Count() ? ProgramPool::Get(operand.program).Source().ToStringView() : StringView());
            }
            return {};
        }

    }  // namespace

    String EffectProgram::Disassemble() const {
        String text;
        for (size_t i = 0; i < _steps.size(); ++i) {
            const EffectStep& step = _steps[i];
            switch (step.op) {
                case EffectOp::Store:
                    text += Format("{}: Store level {} offset {} ({} {} bytes) <- {}\n", i, step.target.level, step.target.offset, EnumUtils<ProgramType>::ToString(step.type), step.bytes,
                                   DescribeOperand(step.operands[0]));
                    break;
                case EffectOp::JumpIfNot:
                    text += Format("{}: JumpIfNot {} -> {}\n", i, DescribeOperand(step.operands[0]), step.jump);
                    break;
                case EffectOp::Jump:
                    text += Format("{}: Jump -> {}\n", i, step.jump);
                    break;
                case EffectOp::Emit: {
                    text += Format("{}: Emit command {}(", i, step.command);
                    for (size_t a = 0; a < step.count && a < kMaxCommandArguments; ++a) {
                        text += Format("{}{}", a == 0 ? "" : ", ", DescribeOperand(step.operands[a]));
                    }
                    text += ")\n";
                    break;
                }
            }
        }
        return text;
    }

}  // namespace BECore
