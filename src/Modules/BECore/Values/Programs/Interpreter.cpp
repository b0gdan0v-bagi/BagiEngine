#include "Interpreter.h"

#include <BECore/Values/Programs/Kernels.h>

namespace BECore {

    void Run(const Program& program, const Context& context, void* result) {
        // Uninitialized on purpose: every instruction writes a register before another reads it, and zeroing a kilobyte would cost more than a small formula
        Array<RegisterImage, kMaxProgramRegisters> registers;  // NOLINT(cppcoreguidelines-pro-type-member-init)
        RegisterImage* regs = registers.data();
        const Span<const RegisterImage> constants = program.Constants();
        for (const Instruction& ins : program.Code()) {
            switch (ins.op) {
                case Opcode::Invalid:
                    ASSERT(false, "Run: an invalid instruction");
                    break;
                case Opcode::LoadConst:
                    Kernel::ExecuteLoadConst(regs, ins, constants);
                    break;
                case Opcode::Copy:
                    Kernel::ExecuteCopy(regs, ins);
                    break;
                case Opcode::LoadTable:
                    Kernel::ExecuteLoadTable(regs, ins, constants);
                    break;
                case Opcode::LoadTableSlot:
                    Kernel::ExecuteLoadTableSlot(regs, ins, constants, context);
                    break;
                case Opcode::CallDomain:
                    Kernel::ExecuteCallDomain(regs, ins, context);
                    break;
#define BE_PROGRAM_CASE_ROW(Op, Type, Cpp)                                                                                                                                                             \
    case Opcode::Op##Type:                                                                                                                                                                             \
        Kernel::Execute<Kernel::Op, Cpp>(regs, ins, context);                                                                                                                                          \
        break;
                    BE_PROGRAM_ROWS(BE_PROGRAM_CASE_ROW)
#undef BE_PROGRAM_CASE_ROW
#define BE_PROGRAM_CASE_FUSED(Op, Type, Cpp)                                                                                                                                                           \
    case Opcode::Op##Type:                                                                                                                                                                             \
        Kernel::Execute##Op<Cpp>(regs, ins, constants, context);                                                                                                                                       \
        break;
                    BE_PROGRAM_FUSED_ROWS(BE_PROGRAM_CASE_FUSED)
#undef BE_PROGRAM_CASE_FUSED
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
        Kernel::StoreResult(regs, program.ResultRegister(), program.ResultType(), result);
    }

}  // namespace BECore
