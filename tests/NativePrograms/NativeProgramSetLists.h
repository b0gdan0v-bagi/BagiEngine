#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore::Tests {

    /**
     * @brief Assembles by hand the loop programs of the set (`Values/Programs/Lists/`), and appends each (once) to @p programs
     *
     * They read a `ProgramListHand` at `kListsLevel`: counts, `any` and `all` with a break, sums, minimums over floats that hold NaN, a loop in a loop, the size of every list and an element by an
     * index that comes from a field (so it is inside the list in some cases and outside in others). A threshold or an index is read from the `ProgramRowOperands` of `kOperandsLevelA`. With the
     * rows of `NativeProgramSetRows.h` they reach every opcode.
     */
    [[nodiscard]] std::expected<void, String> AssembleListPrograms(Vector<ProgramId>& programs);

}  // namespace BECore::Tests
