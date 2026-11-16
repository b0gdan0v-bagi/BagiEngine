#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Instruction.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief Replaces sequences of the code the lowering wrote with fused instructions (`BE_PROGRAM_FUSED_ROWS`)
     *
     * A fused instruction computes what the sequence computes, bit for bit, so a program with and without fusion cannot be told apart by its result; only the code is shorter. A
     * sequence is fused only when the registers it would stop writing are dead afterwards (not read before they are written again, and not @p resultRegister): the code is read as
     * it is, so a program assembled by hand that the lowering would never write is as safe as one it wrote. A field of a fused instruction that the sequence does not fit (a
     * constant index over a byte) leaves the sequence as it is: that is never an error. The constants of the program stay where they are; the fused code names them.
     */
    void Fuse(Vector<Instruction>& code, uint8_t resultRegister);

}  // namespace BECore
