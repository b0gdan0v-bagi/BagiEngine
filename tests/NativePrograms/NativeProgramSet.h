#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueTables.h>
#include <expected>

namespace BECore::Tests {

    /// What the generated file of `BagiEngineTests` says it was generated for
    inline constexpr StringView kNativeProgramSetName = "the engine's test programs";

    /// The levels of `NativeProgramSetDeclaration()`: the formulas read `caster` and `cast`, the hand-assembled programs the three `ProgramRowOperands` after them
    inline constexpr LevelIndex kCasterLevel = 0;
    inline constexpr LevelIndex kCastLevel = 1;
    inline constexpr LevelIndex kOperandsLevelA = 2;
    inline constexpr LevelIndex kOperandsLevelB = 3;
    inline constexpr LevelIndex kOperandsLevelC = 4;
    /// A `ProgramListHand`: the loop programs of `NativeProgramSetLists.h` read its lists
    inline constexpr LevelIndex kListsLevel = 5;

    /**
     * @brief The context the set's programs are compiled under: `caster` is a `ValueUnitStats`, `cast` a `ValueCastInfo`, `a`, `b` and `c` a `ProgramRowOperands` each, `hand` a `ProgramListHand`
     *
     * One declaration for the tool and for the tests: the offsets of the fields are part of a program's code, so a program compiled under another declaration
     * is another program and the generated function would not bind to it.
     */
    [[nodiscard]] const ContextDecl& NativeProgramSetDeclaration();

    /// The tables `ByLevel` reads in the set's formulas (`NativeDamage`): the tool and the tests compile with the same ones, as a table is copied into the code
    [[nodiscard]] const ValueTableSet& NativeProgramSetTables();

    /**
     * @brief Compiles the formulas of the set (`NativeProgramFormulas.h`), each fused and with `fuse = false`, assembles the programs of every row (`NativeProgramSetRows.h`) and returns the programs
     * (each once)
     *
     * The tool that writes the generated file calls it and generates the whole pool; a test calls it so that the same programs are in its pool and bind to the
     * generated functions. A formula that does not compile is the error text: a set with a hole would generate a file that quietly lacks it.
     */
    [[nodiscard]] std::expected<Vector<ProgramId>, String> CompileNativeProgramSet();

}  // namespace BECore::Tests
