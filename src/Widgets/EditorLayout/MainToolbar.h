#pragma once

#include <Widgets/EditorLayout/EditorPlayModel.h>

namespace BECore {

    /**
     * @brief The bar under the menu bar: Play / Stop, Pause and Step in the middle.
     *
     * State only: the buttons change the EditorPlayModel and nothing in the engine reacts yet (the tooltips say so). ImGui calls, so a
     * frame in progress is required; nothing here allocates. The bar is a child of the window that is current, not a window of its
     * own, so it is neither draggable nor part of the saved layout.
     */
    class MainToolbar {
    public:
        /// The height of the bar in ImGui units at the current font and theme: the frame height plus the padding of the theme
        [[nodiscard]] static float GetHeight();

        /// Draws the bar across the width that is free and applies a press to @p model
        void Draw(EditorPlayModel& model);
    };

}  // namespace BECore
