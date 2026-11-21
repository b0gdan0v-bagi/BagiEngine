#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore::Tests {

    /**
     * @brief Assembles by hand the programs that reach every instruction of the set, and appends each (once) to @p programs
     *
     * One program per row of `BE_PROGRAM_ROWS`, `BE_PROGRAM_MIXED_ROWS` and `BE_PROGRAM_CONVERSIONS`, per component of `Extract`, and per width of an enum field, plus `LoadConst`
     * and `Copy` of every type, tables read below, inside and above their range, and both fused instructions, and `LoadElem` of every type (the loop programs are in `NativeProgramSetLists.h`). They
     * are built register by register, not from text, so no rule of the checker keeps an operation from a type it has a row for. Operands are fields of `ProgramRowOperands` read at the offsets the
     * compiler gives (the address of the field in an object) from the levels `kOperandsLevelA` to `kOperandsLevelC`. An operation with more operands than levels reads a second field of the type.
     */
    [[nodiscard]] std::expected<void, String> AssembleRowPrograms(Vector<ProgramId>& programs);

}  // namespace BECore::Tests
