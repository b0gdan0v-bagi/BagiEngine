#pragma once

#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/EffectScript.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/ValueTables.h>
#include <expected>

namespace BECore {

    /// What a caller of `CompileEffects` may change about one compile
    struct EffectOptions {
        /// The paths the script may write (the host's, `WriteTargets`); null permits nothing, so a script that stores anywhere is a load error
        const WriteTargets* targets = nullptr;
        /// The provider of `ByLevel('Name', i)` in the script's expressions: read while the script compiles, never kept
        const IValueTables* tables = nullptr;
        /// Whether the peephole pass fuses the instructions of the script's formulas, as for `CompileExpression`
        bool fuse = true;
    };

    /**
     * @brief The text of a script (`Effects/CLAUDE.md`, "The text of a script") to an `EffectScript`, checked against @p declaration and `options.targets`
     *
     * Every expression is compiled by `CompileExpression` with the type of its place (a field's, `bool` for a condition, a command parameter's), so a literal takes that type, a bare
     * enumerator works and the type and wrap rules are those of formulas; `+=`, `-=`, `++`, `--` and `toggle` are compiled as the assignment of `target + (e)`, `target - (e)`,
     * `target + 1`, `target - 1` and `!target`. A write target must be a whole field of a program type or a registered type that `options.targets` permits; a component is refused. A condition
     * that folds to a constant stays: the script runs what it says. An error is the message, which names the statement, and the 1-based column of the whole text: a mistake in the user's own
     * bytes at its place, one in what the compiler wrote around it at the statement's operator (its keyword for `if`, `emit` and `toggle`).
     *
     * Allocates; it runs when content loads. A refused text leaves the pool of programs as it was: every expression is checked first and compiled only when the whole text passed. (A
     * failure that only lowering can find, such as running out of registers in a very deep formula, comes after that and may leave the programs of the statements before it, which
     * are interned by code and cost nothing twice.)
     */
    [[nodiscard]] std::expected<EffectScript, ProgramError> CompileEffects(StringView text, const ContextDecl& declaration, const EffectOptions& options = {});

}  // namespace BECore
