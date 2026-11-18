#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Lays the text of an edited XML document out like the file it was read from
     *
     * pugixml writes one canonical text (two-space indent, one line per element), so a save of a file a person laid out loses
     * the blank lines between its rows and any other indentation. The inputs are three texts of one file:
     *  - @p original: the file as it is on disk;
     *  - @p baseline: the canonical text of the document parsed from @p original;
     *  - @p updated: the canonical text of the document to save.
     *
     * The result is @p updated with every line that @p baseline and @p updated share kept where it is in @p original, with
     * its original bytes (blank lines, indentation, trailing spaces); a changed or deleted line is replaced by the lines of
     * @p updated (two-space indent), a new line goes directly before the original line that follows it. The final line break
     * follows @p original unless the last line is new. @p baseline equal to @p updated returns @p original itself.
     *
     * Pure: no engine services, no file system, no XML parser. It trusts nothing about the parser: the caller checks that the
     * result parses to the document it saves.
     *
     * @return nullopt, and the caller writes @p updated as it is, when @p original cannot carry the layout: it is empty, holds
     * a `\r`, or its lines do not match @p baseline one by one (blank lines and the indentation apart; a quote style, an
     * `<a/>` form or a wrapped attribute does that); also past a size limit (lines, or edits between @p baseline and @p updated)
     */
    Optional<String> MergeXmlLayout(StringView original, StringView baseline, StringView updated);

}  // namespace BECore
