#pragma once

#include <Widgets/EditorLayout/EditorPlayModel.h>

namespace BECore {

    /**
     * @brief The bar under the menu bar: Play / Stop, Pause and Step in the middle.
     *
     * The buttons change the EditorPlayModel and say which one was pressed; the owner of the bar writes Pause and Step to the engine
     * (EditorLayoutWidget -> GamePause). Play and Stop are state only: the editor does not run the game from here yet (the tooltips say
     * so). ImGui calls, so a frame in progress is required; nothing here allocates. The bar is a child of the window that is current,
     * not a window of its own, so it is neither draggable nor part of the saved layout.
     */
    class MainToolbar {
    public:
        /// The height of the bar in ImGui units at the current font and theme: the frame height plus the padding of the theme
        [[nodiscard]] static float GetHeight();

        /// What the buttons did in one frame
        struct Presses {
            bool play = false;
            bool pause = false;
            bool step = false;  // a step is due: also true on each repeat while the button is held
        };

        /// Draws the bar across the width that is free and applies a press to @p model
        Presses Draw(EditorPlayModel& model);
    };

}  // namespace BECore
