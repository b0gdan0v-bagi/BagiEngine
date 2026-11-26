#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class GameEditorTools;

    /**
     * @brief Draws the debug panels of the active game (`GameEditorTools`) as editor windows.
     *
     * A window's ImGui id is made of the game's prefix and the panel's id, so its place and dock stay in the ImGui settings while another game is active
     * and in a saved layout. It keeps no entry between frames: a callback dies with the registry's own entry, while its game is alive.
     */
    class GameEditorPanels {
    public:
        /// Draws every open panel; a reopened one comes to the front. Once per frame, after the window that hosts the dock space has ended. Null: there
        /// is no registry. A panel's callback may switch the game, which ends the walk
        void Draw(GameEditorTools* tools);

    private:
        struct Window {
            String label;  // the title and the id, as ImGui::Begin takes them
            bool shown = false;
        };

        /// Makes the windows again when the registry is another one or its version moved
        void Sync(const GameEditorTools& tools);

        const GameEditorTools* _tools = nullptr;
        uint64_t _version = 0;
        Vector<Window> _windows;  // parallel to the registry's panels
    };

}  // namespace BECore
