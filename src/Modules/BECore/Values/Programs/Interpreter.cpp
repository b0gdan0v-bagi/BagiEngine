#include "Interpreter.h"

#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/ProgramTraceSink.h>

namespace BECore {

    namespace {

        /// What `Run` passes: nothing is called after an instruction, so the loop of `Run` is the one it had before a traced run existed
        struct NoObserver {};

        /// What `RunTraced` passes: the sink hears each instruction after it ran
        struct SinkObserver {
            ProgramTraceSink& sink;
        };

        template <typename Observer>
        void RunLoop(const Program& program, const Context& context, void* result, [[maybe_unused]] Observer& observer) {
            // Uninitialized on purpose: every instruction writes a register before another reads it, and zeroing a kilobyte would cost more than a small formula
            Array<RegisterImage, kMaxProgramRegisters> registers;  // NOLINT(cppcoreguidelines-pro-type-member-init)
            RegisterImage* regs = registers.data();
            const Span<const RegisterImage> constants = program.Constants();
            // A jump only sets `pc`: the kernels decide it, the generated code branches on the same answers. `Finish` keeps every target inside the code and the loops finite
            const Instruction* const code = program.Code().data();
            const Instruction* const end = code + program.Code().size();
            for (const Instruction* pc = code; pc != end;) {
                const Instruction& ins = *pc++;
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
                    case Opcode::ListSlot:
                        Kernel::ExecuteListSlot(regs, ins, constants, context);
                        break;
                    case Opcode::ListElem:
                        Kernel::ExecuteListElem(regs, ins, constants);
                        break;
                    case Opcode::ListSize:
                        Kernel::ExecuteListSize(regs, ins);
                        break;
                    case Opcode::ListAt:
                        Kernel::ExecuteListAt(regs, ins);
                        break;
                    case Opcode::LoopBegin:
                        pc = Kernel::ExecuteLoopBegin(regs, ins) ? code + ins.imm : pc;
                        break;
                    case Opcode::LoopEnd:
                        pc = Kernel::ExecuteLoopEnd(regs, ins) ? code + ins.imm : pc;
                        break;
                    case Opcode::BreakIf:
                        pc = Kernel::ExecuteBreakIf(regs, ins) ? code + ins.imm : pc;
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
                if constexpr (!std::is_same_v<Observer, NoObserver>) {
                    observer.sink.OnStep(ProgramTraceStep{static_cast<uint32_t>(&ins - code), static_cast<uint32_t>(pc - code), &ins, regs});
                }
            }
            Kernel::StoreResult(regs, program.ResultRegister(), program.ResultType(), result);
        }

    }  // namespace

    void Run(const Program& program, const Context& context, void* result) {
        NoObserver observer;
        RunLoop(program, context, result, observer);
    }

    void RunTraced(const Program& program, const Context& context, void* result, ProgramTraceSink& sink) {
        SinkObserver observer{sink};
        RunLoop(program, context, result, observer);
    }

}  // namespace BECore
