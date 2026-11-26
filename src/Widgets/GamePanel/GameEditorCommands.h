#pragma once

#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <cstdint>

namespace BECore {

    class GameEditorTools;

    /**
     * @brief The commands the palette lists: the editor's static table, then a row per command of the active game (`GameEditorTools`).
     *
     * A game row is `<Game>: <title>` with the control method as its id, no chord and not rebindable. A command whose method reads like an engine command
     * gets no row, so one EditorCommandEvent never means both. No ImGui. Main thread only.
     */
    class GameEditorCommands {
    public:
        GameEditorCommands() = default;
        // The titles of the rows are views into this object
        GameEditorCommands(const GameEditorCommands&) = delete;
        GameEditorCommands& operator=(const GameEditorCommands&) = delete;

        /// Makes the rows again when the registry is another one or its version moved; null: there is no registry
        void Sync(const GameEditorTools* tools);

        /// GetEditorCommands() itself while the game adds no row; valid until the next Sync that moves GetVersion()
        [[nodiscard]] Span<const EditorCommand> GetCommands() const;

        /// Grows whenever the rows were made again: a holder of pointers into GetCommands() drops them then
        [[nodiscard]] uint64_t GetVersion() const {
            return _version;
        }

    private:
        const GameEditorTools* _tools = nullptr;
        uint64_t _toolsVersion = 0;
        uint64_t _version = 0;
        Vector<String> _titles;
        Vector<EditorCommand> _rows;  // empty while the game adds no row
    };

}  // namespace BECore
