#include "ProgramBuilder.h"

#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Peephole.h>

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

    uint32_t ProgramBuilder::AddConstantRun(Span<const RegisterImage> images) {
        const auto first = static_cast<uint32_t>(_constants.size());
        _constants.insert(_constants.end(), images.begin(), images.end());
        return first;
    }

    void ProgramBuilder::Emit(const Instruction& instruction) {
        _code.push_back(instruction);
    }

    void ProgramBuilder::Fuse(uint8_t resultRegister) {
        BECore::Fuse(_code, resultRegister);
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
            const bool loadSlot = IsLoadSlot(ins.op);
            const bool callDomain = ins.op == Opcode::CallDomain;
            const bool tableLoad = ins.op == Opcode::LoadTable;
            const bool slotOnly = IsMulAddSlot(ins.op) || ins.op == Opcode::LoadTableSlot;
            const bool lerpSlots = IsLerpSlots(ins.op);
            // The fused and table instructions keep counts, constant indexes and levels in a, b and c: only a field the opcode reads as a register is one
            // (a load of a slot has a level in a and a size in b; a call has as many registers as the operation has parameters, checked below)
            const bool aIsRegister = !slotOnly && !lerpSlots && !loadSlot && !callDomain;
            const bool bIsRegister = !slotOnly && !lerpSlots && !tableLoad && !loadSlot && !callDomain;
            const bool cIsRegister = !slotOnly && !tableLoad && !loadSlot && !callDomain;
            if ((aIsRegister && ins.a >= kMaxProgramRegisters) || (bIsRegister && ins.b >= kMaxProgramRegisters) || (cIsRegister && ins.c >= kMaxProgramRegisters)) {
                return refuse(i, ins, "an operand names a register beyond the last");
            }
            if (callDomain) {
                if (ins.imm >= ProgramDomain::OperationCount()) {
                    return refuse(i, ins, Format("calls operation {}, but {} are registered", ins.imm, ProgramDomain::OperationCount()));
                }
                const DomainOperation& operation = ProgramDomain::Operation(ins.imm);
                const Array<uint8_t, kMaxDomainArguments> arguments{ins.a, ins.b, ins.c};
                for (size_t argument = 0; argument < operation.parameterCount; ++argument) {
                    if (arguments[argument] >= _highWater) {
                        return refuse(i, ins, Format("'{}' reads r{}, which was never acquired", operation.name.ToStringView(), arguments[argument]));
                    }
                }
                reads |= operation.reads;
            }
            if (ins.op == Opcode::LoadConst && ins.imm >= _constants.size()) {
                return refuse(i, ins, Format("reads constant {}, but there are {}", ins.imm, _constants.size()));
            }
            if (ins.op == Opcode::LoadTable || ins.op == Opcode::LoadTableSlot) {
                const uint32_t count = TableEntryCount(ins);
                const uint32_t first = ins.op == Opcode::LoadTable ? ins.imm : HighHalf(ins.imm);
                if (count == 0) {
                    return refuse(i, ins, "reads a table with no entry");
                }
                if (count > kMaxTableEntries) {
                    return refuse(i, ins, Format("reads a table of {} entries, but a table has at most {}", count, kMaxTableEntries));
                }
                if (static_cast<size_t>(first) + count > _constants.size()) {
                    return refuse(i, ins, Format("reads constants {} to {}, but there are {}", first, static_cast<size_t>(first) + count - 1, _constants.size()));
                }
            }
            if (const Optional<ProgramType> componentType = ComponentOpcodeType(ins.op)) {
                const uint32_t count = ComponentsOf(*componentType).count;
                if (IsPack(ins.op) && ins.a + count > _highWater) {
                    return refuse(i, ins, Format("packs {} operands from r{}, but only {} registers were acquired", count, ins.a, _highWater));
                }
                if (!IsPack(ins.op) && ins.imm >= count) {
                    return refuse(i, ins, Format("takes component {} of a value that has {}", ins.imm, count));
                }
            }
            if (IsMulAddSlot(ins.op) && (ins.b >= _constants.size() || ins.c >= _constants.size())) {
                return refuse(i, ins, Format("reads constants {} and {}, but there are {}", ins.b, ins.c, _constants.size()));
            }
            if (lerpSlots || IsMulAddSlot(ins.op) || ins.op == Opcode::LoadTableSlot) {
                if (ins.a >= kMaxContextLevels || (lerpSlots && ins.b >= kMaxContextLevels)) {
                    return refuse(i, ins, Format("reads a level beyond the {} of a context", kMaxContextLevels));
                }
                reads |= LevelBit(ins.a);
                if (lerpSlots) {
                    reads |= LevelBit(ins.b);
                }
            }
            if (IsLoadSlot(ins.op)) {
                if (ins.a >= kMaxContextLevels) {
                    return refuse(i, ins, Format("reads level {}, but a context has {}", ins.a, kMaxContextLevels));
                }
                if (ins.op == Opcode::LoadSlotDomain && (ins.b == 0 || ins.b > kProgramRegisterBytes)) {
                    return refuse(i, ins, Format("loads a registered type of {} bytes, but a register holds 1 to {}", ins.b, kProgramRegisterBytes));
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
