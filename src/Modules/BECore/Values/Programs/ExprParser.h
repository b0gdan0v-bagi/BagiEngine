#pragma once

#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/Program.h>
#include <expected>

namespace BECore {

    /**
     * @brief The text of a formula as an untyped tree
     *
     * Precedence, loosest first: `||`; `&&`; `==` `!=`; `<` `<=` `>` `>=`; `+` `-`; `*` `/` `%`; unary `-` `!`; the postfix call and member. Binary
     * operators group to the left. A dotted name `a.b.c` is one `Name` and `a.b(c)` a `Call` of the name `a.b`; parentheses leave no node. Every error
     * names what was expected and found and the column; the limits of `ExprLimits` are errors too.
     */
    [[nodiscard]] std::expected<ExprAst, ProgramError> ParseExpression(StringView text);

}  // namespace BECore
