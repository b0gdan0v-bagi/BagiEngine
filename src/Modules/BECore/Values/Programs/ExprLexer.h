#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <expected>

namespace BECore {

    CORE_ENUM(ExprTokenKind, uint8_t, End, Number, String, Name, LeftParen, RightParen, Comma, Dot, Plus, Minus, Star, Slash, Percent, Bang, EqualEqual, BangEqual, Less, LessEqual, Greater,
              GreaterEqual, AndAnd, OrOr, LeftBrace, RightBrace, Semicolon, Equal, PlusEqual, MinusEqual, PlusPlus, MinusMinus)

    /// A token: a view into the text the lexer was given, which must outlive it; the text of a `String` is what is between its quotes, its column the opening quote
    struct ExprToken {
        ExprTokenKind kind = ExprTokenKind::End;
        uint32_t column = 0;
        StringView text;
        bool isFloat = false;
    };

    /// `statements` adds the tokens of a script (`{ } ; = += -= ++ --`; `Effects/CLAUDE.md`), which a formula refuses; `maxBytes` is the longest text taken
    struct LexOptions {
        bool statements = false;
        size_t maxBytes = ExprLimits::kMaxTextBytes;
    };

    /**
     * @brief The tokens of a formula, the last one `End`
     *
     * Refuses a text over `ExprLimits::kMaxTextBytes`, a byte that is no part of the language (a control byte, a byte of invalid UTF-8 or any non-ASCII
     * character are named as such), a `'string'` that is not closed or holds anything but printable ASCII (no escapes: a string cannot hold `'`) and a malformed number (`1..2`, `1e`, `12ab`). `[` is
     * refused with the reason: a path must name a fixed field. With `options.statements` the script tokens are read and the limit is `options.maxBytes` (a message says "script").
     */
    [[nodiscard]] std::expected<Vector<ExprToken>, ProgramError> LexExpression(StringView text, const LexOptions& options = {});

    /// How a token reads in a message: `'+'`, `the string 'Fire'`, `the end of the text`
    [[nodiscard]] String DescribeToken(const ExprToken& token);

}  // namespace BECore
