#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Types/Types.h>
#include <expected>

namespace BECore {

    /// A command of the Console's command line: the control method and its parameters (always a JSON object, as its compact text)
    struct ConsoleCommand {
        String method;
        String params;
    };

    /**
     * @brief Reads what a person typed into the command line.
     *
     * `method`, `method {"a":1}` (the rest is one JSON object) or `method key=value ...`. In the last form the words are split on blanks outside
     * double quotes and outside balanced `{}` and `[]`; a value is read as JSON when it is one (`1`, `true`, `"a b"`, `[1,2]`) and is the text itself
     * when it is not (`name=e2e4`). A word without `=`, an empty or repeated key, a `null`, an unbalanced bracket or quote and a nesting the control API
     * would refuse are errors; the message is one line for the output area. Pure, never aborts on any text (the JSON rules of Control/CLAUDE.md).
     * The parameters come back as the compact text of the object with its keys sorted; of a repeated key in the brace form the first counts, a number
     * beyond a double (`1e999`) is refused in the brace form and is a text in the `key=value` form.
     */
    [[nodiscard]] std::expected<ConsoleCommand, String> ParseConsoleCommand(StringView text);

    /// The parameters alone, as ParseConsoleCommand reads them after the method: a JSON object or `key=value ...`, as the compact text of the object
    /// (`{}` for blank text)
    [[nodiscard]] std::expected<String, String> ParseConsoleParams(StringView text);

    /// What Tab does: the line to put in the input and the candidates to show above it (empty when there is nothing to choose from)
    struct ConsoleCompletion {
        String text;
        Vector<String> candidates;
    };

    /**
     * @brief Completes the end of @p text (the whole line of the input).
     *
     * In the first word the method names are matched by prefix: one match is completed and followed by a blank, several are completed to their
     * longest common prefix and listed. After the method, in the `key=value` form, the parameter names of its description that are not given yet
     * are completed to `name=`; the candidates list the required ones first, each with its type. Nothing is offered in the JSON form, inside an
     * open quote or bracket, after a finished `key=value`, or for a method without a description. A text with nothing to complete comes back as it was.
     */
    [[nodiscard]] ConsoleCompletion CompleteConsoleCommand(StringView text, Span<const ControlMethodListing> methods);

}  // namespace BECore
