#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <expected>

namespace BECore {

    CORE_ENUM(ExprTokenKind, uint8_t, End, Number, String, Name, LeftParen, RightParen, Comma, Dot, Plus, Minus, Star, Slash, Percent, Bang, EqualEqual, BangEqual, Less, LessEqual, Greater,
              GreaterEqual, AndAnd, OrOr, LeftBracket, RightBracket)

    /// A token: a view into the text the lexer was given, which must outlive it; the text of a `String` is what is between its quotes, its column the opening quote
    struct ExprToken {
        ExprTokenKind kind = ExprTokenKind::End;
        uint32_t column = 0;
        StringView text;
        bool isFloat = false;
    };

    /**
     * @brief The tokens of a formula, the last one `End`
     *
     * Refuses a text over `ExprLimits::kMaxTextBytes`, a byte that is no part of the language (a control byte, a byte of invalid UTF-8 or any non-ASCII
     * character are named as such), a `'string'` that is not closed or holds anything but printable ASCII (no escapes: a string cannot hold `'`) and a malformed number (`1..2`, `1e`, `12ab`). `[` and
     * `]` are tokens of an index; what may be indexed is the checker's business.
     */
    [[nodiscard]] std::expected<Vector<ExprToken>, ProgramError> LexExpression(StringView text);

    /// How a token reads in a message: `'+'`, `the string 'Fire'`, `the end of the text`
    [[nodiscard]] String DescribeToken(const ExprToken& token);

}  // namespace BECore
