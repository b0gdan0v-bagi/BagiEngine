#pragma once

namespace BECore::UIText {

    /// The most bytes of a message that repeats text of a content file (a key, a selector, a value): the game logs it, and a file can make that text as long as it likes
    inline constexpr size_t kMaxMessageBytes = 300;

    /**
     * @brief @p text as a line of a log: a control character (a line break too) becomes a space, and a text longer than kMaxMessageBytes is cut at a UTF-8 character with `...` after
     *
     * For the messages of the loaders of content (`UIStrings`, `UIThemes`, `UIWebReport`), whose text has pieces of the file in it.
     */
    [[nodiscard]] String TidyMessage(StringView text);

    /// The most bytes of a piece of the file that a message repeats before its own words (`selector '<piece>' is left out: ...`): the cut of TidyMessage is at the end of the
    /// message, where the reason is
    inline constexpr size_t kMaxExcerptBytes = 80;

    /// The first kMaxExcerptBytes of @p text (cut at a UTF-8 character, `...` after) with its control characters as spaces
    [[nodiscard]] String Excerpt(StringView text);

}  // namespace BECore::UIText
