#pragma once

#include <BECore/Values/Effects/ScriptAst.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore {

    /**
     * @brief The statements of a script and the byte range of each expression (the grammar is in `Effects/CLAUDE.md`, "The text of a script")
     *
     * Only the shape is checked: names, types and targets are the compiler's. An empty script is valid. Every error carries the 1-based column in the whole text; a missing `;` is
     * reported at the end of the previous token. Refuses a text over `ScriptLimits::kMaxTextBytes`, more than `kMaxStatements` statements and blocks nested deeper than `kMaxBlockDepth`.
     */
    [[nodiscard]] std::expected<ScriptAst, ProgramError> ParseScript(StringView text);

}  // namespace BECore
