#pragma once

#include <BECore/Values/Programs/Instruction.h>
#include <cstdint>

namespace BECore {

    /// One executed instruction, as the interpreter reports it after it ran
    struct ProgramTraceStep {
        /// The index of the instruction in `Program::Code()`
        uint32_t pc = 0;
        /// The index the run goes to next: `pc + 1`, or the target of a jump that was taken (`Code().size()` when the run ends)
        uint32_t nextPc = 0;
        const Instruction* instruction = nullptr;
        /// The whole register file after the instruction, valid during the call only: the register the instruction wrote holds its result
        const RegisterImage* registers = nullptr;
    };

    /// What `RunTraced` tells: an observer of a run
    class ProgramTraceSink {
    public:
        virtual ~ProgramTraceSink() = default;

        virtual void OnStep(const ProgramTraceStep& step) = 0;
    };

}  // namespace BECore
