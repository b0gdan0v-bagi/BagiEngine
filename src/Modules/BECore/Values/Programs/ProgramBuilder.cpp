#include "ProgramBuilder.h"

namespace BECore {

    std::expected<uint8_t, ProgramError> ProgramBuilder::AcquireRegister() {
        if (_live >= kMaxProgramRegisters) {
            return std::unexpected(ProgramError{Format("the formula needs more than {} registers at once: split it or nest it less", kMaxProgramRegisters), 0});
        }
        const uint8_t reg = _live++;
        _highWater = std::max(_highWater, _live);
        return reg;
    }

    void ProgramBuilder::ReleaseRegister(uint8_t reg) {
        ASSERT(_live > 0 && reg + 1 == _live, "ProgramBuilder: registers are released in the reverse order they were acquired");
        --_live;
    }

    uint32_t ProgramBuilder::AddConstant(const RegisterImage& image) {
        const auto found = std::find(_constants.begin(), _constants.end(), image);
        if (found != _constants.end()) {
            return static_cast<uint32_t>(found - _constants.begin());
        }
        _constants.push_back(image);
        return static_cast<uint32_t>(_constants.size() - 1);
    }

    void ProgramBuilder::Emit(const Instruction& instruction) {
        _code.push_back(instruction);
    }

    std::expected<Program, ProgramError> ProgramBuilder::Finish(ProgramType resultType, uint8_t resultRegister, PoolString source) const {
        const auto refuse = [](size_t index, const Instruction& ins, StringView what) {
            return std::unexpected(ProgramError{Format("instruction {} ({}): {}", index, EnumUtils<Opcode>::ToString(ins.op), what), 0});
        };
        if (_code.empty()) {
            return std::unexpected(ProgramError{"the program has no instruction", 0});
        }
        if (resultRegister >= _highWater) {
            return std::unexpected(ProgramError{Format("the result register r{} was never acquired", resultRegister), 0});
        }
        LevelMask reads = 0;
        for (size_t i = 0; i < _code.size(); ++i) {
            const Instruction& ins = _code[i];
            if (ins.op == Opcode::Invalid) {
                return refuse(i, ins, "not an operation");
            }
            if (ins.dst >= _highWater) {
                return refuse(i, ins, Format("writes r{}, which was never acquired", ins.dst));
            }
            if (ins.a >= kMaxProgramRegisters || ins.b >= kMaxProgramRegisters || ins.c >= kMaxProgramRegisters) {
                return refuse(i, ins, "an operand names a register beyond the last");
            }
            if (ins.op == Opcode::LoadConst && ins.imm >= _constants.size()) {
                return refuse(i, ins, Format("reads constant {}, but there are {}", ins.imm, _constants.size()));
            }
            if (IsLoadSlot(ins.op)) {
                if (ins.a >= kMaxContextLevels) {
                    return refuse(i, ins, Format("reads level {}, but a context has {}", ins.a, kMaxContextLevels));
                }
                if (ins.op == Opcode::LoadSlotEnum && !IsEnumFieldBytes(ins.b)) {
                    return refuse(i, ins, Format("loads an enum of {} bytes, but an enum field is 1, 2, 4 or 8", ins.b));
                }
                reads |= LevelBit(ins.a);
            }
        }
        Program program;
        program._resultType = resultType;
        program._resultRegister = resultRegister;
        program._registerCount = _highWater;
        program._reads = reads;
        program._code = _code;
        program._constants = _constants;
        program._source = source;
        return program;
    }

}  // namespace BECore
