#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief Turns the command template of the preference `externalEditor` into an argument list.
     *
     * The template is split on blanks; a double quote at the start of a word keeps the word together up to the closing quote, which has to be
     * followed by a blank or the end (a quote inside a word and an unterminated quote are an error). `{file}` and `{line}` are replaced
     * **after** the split, inside an argument, and the replacement is never split or scanned again, so a path with spaces, quotes or `;`
     * stays one argument. A template without `{line}` gets no line. An empty or blank template, or one with an error, gives nullopt (the
     * caller falls back to the system opener).
     *
     * Pure, no process is started.
     */
    [[nodiscard]] Optional<Vector<String>> BuildEditorLaunch(StringView launchTemplate, StringView file, uint32_t line);

    /// The `file://` URL of @p path: back slashes become `/`, bytes outside the unreserved set (and `/`, `:`) are percent-encoded
    [[nodiscard]] String FileUrlOf(StringView path);

    /// Whether a log row with this `file` can be opened: an absolute path that exists. @p file must be a string of static lifetime (the
    /// `__FILE__` of a log call): the answer is cached per pointer, so a frame does not touch the disk per row and the cache is as big as the
    /// number of source files. Main thread only.
    [[nodiscard]] bool IsSiteOpenable(const char* file);

    /**
     * @brief Opens @p file at @p line.
     *
     * With a template the program is started through SDL_CreateProcess with an argument array, never through a shell, and not waited for;
     * without one (or an unusable one) the file goes to the system opener as a `file://` URL, without the line. On failure it returns false and
     * @p error holds the text from SDL.
     */
    bool LaunchEditor(StringView launchTemplate, const char* file, uint32_t line, String& error);

}  // namespace BECore
