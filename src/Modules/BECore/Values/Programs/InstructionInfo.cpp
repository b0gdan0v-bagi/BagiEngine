#include "InstructionInfo.h"

#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Kernels.h>

namespace BECore {

    namespace {

        /// The registers a row reads, by the shape of its operation
        template <typename Op, typename T>
        constexpr RegisterMask RowReads(const Instruction& ins) {
            if constexpr (Op::kShape == Kernel::OpShape::Slot) {
                return 0;
            } else if constexpr (Op::kShape == Kernel::OpShape::Unary || Op::kShape == Kernel::OpShape::Element || Op::kShape == Kernel::OpShape::Extract) {
                return RegisterBit(ins.a);
            } else if constexpr (Op::kShape == Kernel::OpShape::Binary || Op::kShape == Kernel::OpShape::Compare) {
                return RegisterBit(ins.a) | RegisterBit(ins.b);
            } else if constexpr (Op::kShape == Kernel::OpShape::Pack) {
                RegisterMask mask = 0;
                for (uint32_t i = 0; i < ComponentTraits<T>::kCount; ++i) {
                    mask |= RegisterBit(ins.a + i);
                }
                return mask;
            } else {
                return RegisterBit(ins.a) | RegisterBit(ins.b) | RegisterBit(ins.c);
            }
        }

        /// The registers a fused instruction reads: the weight of `LerpSlots`; the others read fields only
        RegisterMask FusedReads(const Instruction& ins) {
            return IsLerpSlots(ins.op) ? RegisterBit(ins.c) : RegisterMask{0};
        }

        RegisterMask ReadsOf(const Instruction& ins) {
            switch (ins.op) {
                case Opcode::Invalid:
                case Opcode::LoadConst:
                    return 0;
                case Opcode::LoadTableSlot:
                    return 0;
                case Opcode::CallDomain: {
                    // As many registers as the operation has parameters; an operation that is not registered leaves the pass blind, as an opcode it does not know
                    if (ins.imm >= ProgramDomain::OperationCount()) {
                        return ~RegisterMask{0};
                    }
                    const uint32_t count = ProgramDomain::Operation(ins.imm).parameterCount;
                    return (count > 0 ? RegisterBit(ins.a) : 0) | (count > 1 ? RegisterBit(ins.b) : 0) | (count > 2 ? RegisterBit(ins.c) : 0);
                }
                case Opcode::Copy:
                case Opcode::LoadTable:
                    return RegisterBit(ins.a);
                case Opcode::ListSlot:
                    return 0;
                case Opcode::ListElem:
                case Opcode::ListSize:
                case Opcode::BreakIf:
                    return RegisterBit(ins.a);
                case Opcode::ListAt:
                    return RegisterBit(ins.a) | RegisterBit(ins.b);
                case Opcode::LoopBegin:
                    return RegisterBit(ins.a);
                case Opcode::LoopEnd:
                    return RegisterBit(ins.a) | RegisterBit(ins.dst);
#define BE_INSTRUCTION_READS_ROW(Op, Type, Cpp)                                                                                                                                                        \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return RowReads<Kernel::Op, Cpp>(ins);
                    BE_PROGRAM_ROWS(BE_INSTRUCTION_READS_ROW)
#undef BE_INSTRUCTION_READS_ROW
#define BE_INSTRUCTION_READS_FUSED(Op, Type, Cpp)                                                                                                                                                      \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return FusedReads(ins);
                    BE_PROGRAM_FUSED_ROWS(BE_INSTRUCTION_READS_FUSED)
#undef BE_INSTRUCTION_READS_FUSED
#define BE_INSTRUCTION_READS_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                       \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        return RegisterBit(ins.a) | RegisterBit(ins.b);
                    BE_PROGRAM_MIXED_ROWS(BE_INSTRUCTION_READS_MIXED)
#undef BE_INSTRUCTION_READS_MIXED
#define BE_INSTRUCTION_READS_CONVERSION(Src, SrcCpp, Dst, DstCpp)                                                                                                                                      \
    case Opcode::Src##To##Dst:                                                                                                                                                                         \
        return RegisterBit(ins.a);
                    BE_PROGRAM_CONVERSIONS(BE_INSTRUCTION_READS_CONVERSION)
#undef BE_INSTRUCTION_READS_CONVERSION
            }
            return ~RegisterMask{0};
        }

        Optional<ProgramOperation> OperationOf(Opcode op) {
            switch (op) {
#define BE_INSTRUCTION_OPERATION_ROW(Op, Type, Cpp)                                                                                                                                                    \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return ProgramOperation::Op;
                BE_PROGRAM_ROWS(BE_INSTRUCTION_OPERATION_ROW)
#undef BE_INSTRUCTION_OPERATION_ROW
#define BE_INSTRUCTION_OPERATION_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                   \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        return ProgramOperation::Op;
                BE_PROGRAM_MIXED_ROWS(BE_INSTRUCTION_OPERATION_MIXED)
#undef BE_INSTRUCTION_OPERATION_MIXED
#define BE_INSTRUCTION_OPERATION_NONE_FUSED(Op, Type, Cpp) case Opcode::Op##Type:
                BE_PROGRAM_FUSED_ROWS(BE_INSTRUCTION_OPERATION_NONE_FUSED)
#undef BE_INSTRUCTION_OPERATION_NONE_FUSED
#define BE_INSTRUCTION_OPERATION_NONE_CONVERSION(Src, SrcCpp, Dst, DstCpp) case Opcode::Src##To##Dst:
                BE_PROGRAM_CONVERSIONS(BE_INSTRUCTION_OPERATION_NONE_CONVERSION)
#undef BE_INSTRUCTION_OPERATION_NONE_CONVERSION
                case Opcode::Invalid:
                case Opcode::LoadConst:
                case Opcode::Copy:
                case Opcode::LoadTable:
                case Opcode::LoadTableSlot:
                case Opcode::CallDomain:
                case Opcode::ListSlot:
                case Opcode::ListElem:
                case Opcode::ListSize:
                case Opcode::ListAt:
                case Opcode::LoopBegin:
                case Opcode::LoopEnd:
                case Opcode::BreakIf:
                    break;
            }
            return std::nullopt;
        }

        Optional<ProgramType> ResultTypeOfInstruction(const Instruction& ins) {
            if (ins.op != Opcode::CallDomain) {
                return ResultTypeOf(ins.op);
            }
            if (ins.imm >= ProgramDomain::OperationCount()) {
                return std::nullopt;
            }
            return ProgramDomain::Operation(ins.imm).result.type;
        }

    }  // namespace

    InstructionInfo DescribeInstruction(const Instruction& instruction) {
        return InstructionInfo{ReadsOf(instruction), ResultTypeOfInstruction(instruction), OperationOf(instruction.op)};
    }

}  // namespace BECore
