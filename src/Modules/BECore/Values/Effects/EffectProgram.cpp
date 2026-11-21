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

        [[nodiscard]] bool SameOperand(const EffectOperand& a, const EffectOperand& b) {
            if (a.kind != b.kind || a.type != b.type || a.bytes != b.bytes) {
                return false;
            }
            switch (a.kind) {
                case CompiledKind::Constant:
                    return a.constant.bytes == b.constant.bytes;
                case CompiledKind::Slot:
                    return a.slot.level == b.slot.level && a.slot.offset == b.slot.offset;
                case CompiledKind::Program:
                    return a.program == b.program;
            }
            return false;
        }

        [[nodiscard]] bool SameStep(const EffectStep& a, const EffectStep& b) {
            if (a.op != b.op || a.type != b.type || a.bytes != b.bytes || a.count != b.count || a.jump != b.jump || a.command != b.command || a.target.level != b.target.level ||
                a.target.offset != b.target.offset) {
                return false;
            }
            for (size_t i = 0; i < a.operands.size(); ++i) {
                if (!SameOperand(a.operands[i], b.operands[i])) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    bool EffectProgram::Equivalent(const EffectProgram& other) const {
        if (_source != other._source || _steps.size() != other._steps.size()) {
            return false;
        }
        for (size_t i = 0; i < _steps.size(); ++i) {
            if (!SameStep(_steps[i], other._steps[i])) {
                return false;
            }
        }
        return true;
    }

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
