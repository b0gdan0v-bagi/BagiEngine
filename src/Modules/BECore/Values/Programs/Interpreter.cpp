#include "Interpreter.h"

#include <BECore/Values/Programs/Kernels.h>

namespace BECore {

    void Run(const Program& program, const Context& context, void* result) {
        // Uninitialized on purpose: every instruction writes a register before another reads it, and zeroing 512 bytes would cost more than a small formula
        Array<RegisterImage, kMaxProgramRegisters> registers;  // NOLINT(cppcoreguidelines-pro-type-member-init)
        RegisterImage* regs = registers.data();
        const Span<const RegisterImage> constants = program.Constants();
        for (const Instruction& ins : program.Code()) {
            switch (ins.op) {
                case Opcode::Invalid:
                    ASSERT(false, "Run: an invalid instruction");
                    break;
                case Opcode::LoadConst:
                    ASSERT(ins.imm < constants.size());
                    regs[ins.dst] = constants[ins.imm];
                    break;
                case Opcode::Copy:
                    regs[ins.dst] = regs[ins.a];
                    break;
                case Opcode::LoadTable: {
                    const uint32_t count = TableEntryCount(ins);
                    ASSERT(count > 0 && ins.imm + count <= constants.size());
                    regs[ins.dst] = constants[ins.imm + Kernel::TableIndex(Kernel::Load<int32_t>(regs[ins.a]), count)];
                    break;
                }
#define BE_PROGRAM_CASE_ROW(Op, Type, Cpp)                                                                                                                                                             \
    case Opcode::Op##Type:                                                                                                                                                                             \
        Kernel::Execute<Kernel::Op, Cpp>(regs, ins, context);                                                                                                                                          \
        break;
                    BE_PROGRAM_ROWS(BE_PROGRAM_CASE_ROW)
#undef BE_PROGRAM_CASE_ROW
#define BE_PROGRAM_CASE_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                            \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        Kernel::ExecuteMixed<Kernel::Op, LCpp, RCpp>(regs, ins);                                                                                                                                       \
        break;
                    BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_CASE_MIXED)
#undef BE_PROGRAM_CASE_MIXED
#define BE_PROGRAM_CASE_CONVERSION(Src, SrcCpp, Dst, DstCpp)                                                                                                                                           \
    case Opcode::Src##To##Dst:                                                                                                                                                                         \
        Kernel::Store<DstCpp>(regs[ins.dst], Kernel::Convert<SrcCpp, DstCpp>(Kernel::Load<SrcCpp>(regs[ins.a])));                                                                                      \
        break;
                    BE_PROGRAM_CONVERSIONS(BE_PROGRAM_CASE_CONVERSION)
#undef BE_PROGRAM_CASE_CONVERSION
            }
        }
        std::memcpy(result, regs[program.ResultRegister()].bytes.data(), ProgramTypeSize(program.ResultType()));
    }

}  // namespace BECore
