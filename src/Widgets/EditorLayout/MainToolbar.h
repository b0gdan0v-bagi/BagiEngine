#pragma once

namespace BECore {

    /**
     * @brief The bar under the menu bar: the frame of the asset `MainToolbarPanel` (Play / Stop, Pause and Step in the middle).
     *
     * The buttons are the asset's (`config/panels/MainToolbarPanel.xml`, a `Bar` that `PanelBarsRegistry` hands over); they run the commands `game.play`,
     * `game.pause` and `game.step` of `EditorLayoutWidget`. What is left here is the child that frames them: its height and the panel colour. ImGui calls,
     * so a frame in progress is required. The bar is a child of the window that is current, not a window of its own, so it is neither draggable nor part of
     * the saved layout. A game whose `WidgetsConfig.xml` does not list the asset gets the empty child.
     */
    class MainToolbar {
    public:
        /// The height of the bar in ImGui units at the current font and theme: the frame height plus the padding of the theme
        [[nodiscard]] static float GetHeight();

        /// Draws the child across the width that is free and the asset's buttons in it
        static void Draw();
    };

}  // namespace BECore
