#pragma once

#include <BECore/Types/Types.h>
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
    [[nodiscard]] Vector<const EditorCommand*> FilterCommands(StringView query, Span<const EditorCommand> commands);

    /// FilterCommands into @p out: clears it and keeps its capacity, so a caller that keeps the vector allocates only while it grows.
    /// Scores in the thread's ScratchArena; allocates nothing from the heap once the arena has its chunk.
    void FilterCommandsInto(StringView query, Span<const EditorCommand> commands, Vector<const EditorCommand*>& out);

    /**
     * @brief The filtered list of the palette, recomputed only when the query or the command table changes.
     *
     * The palette asks every frame; the filter runs on the frames where the answer differs from the last one.
     * The pointers point into the span given to Update, so GetMatches() is valid until the next Update.
     */
    class CommandMatchList {
    public:
        /// Refilters when @p query or the span (its data and size) differs from the last call; returns whether it did
        bool Update(StringView query, Span<const EditorCommand> commands);

        /// The commands that match the last query, best first; valid until the next Update
        [[nodiscard]] const Vector<const EditorCommand*>& GetMatches() const {
            return _matches;
        }

        /// The next Update refilters
        void Invalidate() {
            _valid = false;
        }

    private:
        Vector<const EditorCommand*> _matches;
        String _query;
        const EditorCommand* _commands = nullptr;
        size_t _commandCount = 0;
        bool _valid = false;
    };

    /// The row selected after moving @p delta rows from @p selected in a list of @p count rows: wraps at both ends, 0 for an
    /// empty list. A @p selected outside the list is brought into it first.
    [[nodiscard]] int MoveSelection(int selected, int delta, int count);

}  // namespace BECore
