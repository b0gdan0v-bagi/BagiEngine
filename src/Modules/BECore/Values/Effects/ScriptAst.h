#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// What hostile content may ask of the script parser; each is a load error that names the limit
    namespace ScriptLimits {
        inline constexpr size_t kMaxTextBytes = 16384;
        /// All blocks together, nested ones included
        inline constexpr size_t kMaxStatements = 256;
        inline constexpr size_t kMaxBlockDepth = 8;
    }  // namespace ScriptLimits

    /// The place of a statement in `ScriptAst::statements`
    using ScriptStatementId = uint32_t;

    /// A range of bytes of the script text, `begin` included and `end` not: the text of one expression, which the compiler compiles as a formula and whose columns it shifts by `begin`
    struct ScriptRange {
        uint32_t begin = 0;
        uint32_t end = 0;
    };

    /// The statements of a block in order: `ScriptAst::order[first, first + count)`
    struct ScriptBlock {
        uint32_t first = 0;
        uint32_t count = 0;
    };

    CORE_ENUM(ScriptStatementKind, uint8_t, Assign, AddAssign, SubAssign, Increment, Decrement, Toggle, If, Emit)

    /**
     * @brief One statement of a script
     *
     * `target` is the dotted path written to (`Assign` to `Toggle`) or the command name (`Emit`), at `targetColumn`; `column` is the one an error of the statement points at: the operator
     * of an assignment, the keyword of the others. `target` is an `ArenaString` and not a view of the text because the parser joins the segments of a dotted path (`a . b` is `a.b`). `expression` is
     * the right side of `Assign`, `AddAssign` and `SubAssign` and the condition of `If`; `Emit` has its arguments in `ScriptAst::arguments[firstArgument, firstArgument + argumentCount)`. An `else if`
     * is an `elseBlock` of the one `If` statement. Columns are 1-based bytes of the whole text.
     */
    struct ScriptStatement {
        ScriptStatementKind kind = ScriptStatementKind::Assign;
        uint32_t column = 0;
        uint32_t targetColumn = 0;
        ArenaString target;
        ScriptRange expression;
        uint32_t firstArgument = 0;
        uint32_t argumentCount = 0;
        ScriptBlock thenBlock;
        bool hasElse = false;
        ScriptBlock elseBlock;
    };

    /**
     * @brief The statements of a parsed script: by id, the blocks as lists of ids, the arguments of emits in one list
     *
     * Every container is on the thread's scratch arena, so a tree belongs to the `ScratchScope` that is alive when it is made and must not outlive it, be stored in a longer-lived
     * object or cross threads (`Memory/CLAUDE.md`, "Scratch arena and scopes").
     */
    struct ScriptAst {
        ArenaVector<ScriptStatement> statements;
        ArenaVector<ScriptStatementId> order;
        ArenaVector<ScriptRange> arguments;
        ScriptBlock root;

        [[nodiscard]] const ScriptStatement& operator[](ScriptStatementId id) const {
            return statements[id];
        }

        [[nodiscard]] ScriptStatementId At(ScriptBlock block, uint32_t index) const {
            return order[block.first + index];
        }
    };

}  // namespace BECore
