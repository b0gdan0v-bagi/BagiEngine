#pragma once

#include <Widgets/EditorLayout/EditorCommands.h>

namespace BECore {

    /**
     * @brief The commands the palette lists for @p query, best match first.
     *
     * A command matches when the characters of the query (spaces ignored, case ignored) appear in its title or in its id
     * in that order. The score rewards a match at the start of the title, at the start of a word and in a run, so "lay"
     * puts "Reset Layout" before a title that only has an l, an a and a y far apart. A match in the title beats one in the id.
     * Commands with the same score keep the order of @p commands. An empty query (or spaces only) lists all of them in
     * their order.
     *
     * Pure, no ImGui context.
     */
    [[nodiscard]] eastl::vector<const EditorCommand*> FilterCommands(eastl::string_view query, eastl::span<const EditorCommand> commands);

}  // namespace BECore
