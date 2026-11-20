#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Program.h>

namespace BECore {

    struct NativeCodegenOptions {
        /// Named in the header comment of the file, so a reader knows which content the functions were generated for
        StringView contentSet;
    };

    /**
     * @brief The C++ statement that runs @p ins, or an empty string for an opcode that has none (`Invalid`)
     *
     * The statement calls the kernel of the opcode (`Kernels.h`) with the instruction written as a literal, so a compiler sees its register numbers and offsets.
     * It is expanded from the same lists as the interpreter's `switch`, so a row cannot be missing. @p constants is the C++ expression of the program's constants,
     * for the instructions that read them.
     */
    [[nodiscard]] String EmitNativeInstruction(const Instruction& ins, StringView constants);

    /**
     * @brief One C++ translation unit with a function for each of @p programs, a table of `NativeProgramEntry` and a `NativeProgramRegistrar` of it
     *
     * Deterministic: the programs are ordered by `CodeHash` (a tie by the code), equal programs are written once, and nothing of the process (an address, an id, the
     * time, a path) enters the text, so two calls give equal bytes and a file under version control changes only when a program does. Each function does what `Run`
     * does with the same kernels, with the instructions written out. The text is only the generator's: compiling it is the build's (`be_add_native_programs`).
     */
    [[nodiscard]] String GenerateNativeSource(Span<const ProgramId> programs, const NativeCodegenOptions& options);

}  // namespace BECore
