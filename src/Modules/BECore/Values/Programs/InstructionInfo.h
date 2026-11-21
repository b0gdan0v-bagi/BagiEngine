#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstdint>
#include <limits>

namespace BECore {

    /// A set of registers, a bit per register
    using RegisterMask = uint32_t;
    static_assert(kMaxProgramRegisters <= std::numeric_limits<RegisterMask>::digits, "a RegisterMask holds a bit per register");

    /// The bit of @p reg, or no bit for a number beyond the registers
    [[nodiscard]] constexpr RegisterMask RegisterBit(uint32_t reg) {
        return reg < kMaxProgramRegisters ? RegisterMask{1} << reg : RegisterMask{0};
    }

    /// What one instruction does to the registers and what it is, read from the instruction alone
    struct InstructionInfo {
        /// Every register the instruction reads: what must stay intact for it. A `CallDomain` of an operation that is not registered, and an opcode this file does not know, read every register
        RegisterMask reads = 0;
        /// The type of the value written to `dst`. Empty for `LoadConst`, `Copy`, `LoadTable` and `LoadTableSlot` (they move a register whose type only the program knows) and for the list and
        /// loop instructions that write a list or an element image (`ListSize` is `Int32`); for a `CallDomain` the result of the operation (empty when it is not registered)
        Optional<ProgramType> resultType;
        /// The operation of the language the instruction is: the row it belongs to (`LoadSlot`, `Add`, `Extract`, `Pack`, a mixed row's `Mul`). Empty for what has no such name: the moves and
        /// tables, the conversions, the fused instructions (the opcode tells them), `CallDomain` and the list instructions
        Optional<ProgramOperation> operation;
    };

    /**
     * @brief Which registers @p instruction reads, the type it writes and the operation it is
     *
     * The one table of the operand registers of an opcode: the peephole pass decides what a fusion may drop by it, and the explanation of a run (`Values/Explain/`) turns the
     * instructions into a tree by it, so the two cannot disagree. Built from the same rows as the interpreter, so a new opcode that the switches lack is a compile error.
     * A `LoopEnd` reads its element register too (the next element is computed from it).
     */
    [[nodiscard]] InstructionInfo DescribeInstruction(const Instruction& instruction);

}  // namespace BECore
