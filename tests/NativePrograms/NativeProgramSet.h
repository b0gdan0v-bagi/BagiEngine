#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore::Tests {

    /// What the generated file of `BagiEngineTests` says it was generated for
    inline constexpr StringView kNativeProgramSetName = "the engine's test programs";

    /**
     * @brief The context the set's formulas are compiled under: `caster` is a `ValueUnitStats` (level 0) and `cast` a `ValueCastInfo` (level 1)
     *
     * One declaration for the tool and for the tests: the offsets of the fields are part of a program's code, so a program compiled under another declaration
     * is another program and the generated function would not bind to it.
     */
    [[nodiscard]] const ContextDecl& NativeProgramSetDeclaration();

    /**
     * @brief Compiles the formulas of the set, each fused and with `fuse = false`, and returns the programs they became (each once, in the order of the formulas)
     *
     * The tool that writes the generated file calls it and generates the whole pool; a test calls it so that the same programs are in its pool and bind to the
     * generated functions. A formula that does not compile is the error text: a set with a hole would generate a file that quietly lacks it.
     */
    [[nodiscard]] std::expected<Vector<ProgramId>, String> CompileNativeProgramSet();

}  // namespace BECore::Tests
