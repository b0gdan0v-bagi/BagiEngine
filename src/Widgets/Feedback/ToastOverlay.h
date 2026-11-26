#pragma once

#include <Widgets/Feedback/ToastFeed.h>

namespace BECore {

    class ControlActivity;

    /**
     * @brief The toasts on screen: a column of cards in the bottom-right corner of the main viewport, the newest at the bottom
     *
     * One ImGui window, begun only while a toast lives, kept above the docked and the floating windows; it takes no input and no focus. A card is one
     * line of its text, cut to the card's width. ImGui calls: a frame in progress is required.
     */
    class ToastOverlay {
    public:
        /// ToastFeed::Connect
        void Connect(const ControlActivity& activity) {
            _feed.Connect(activity);
        }

        /// Ages the toasts by the frame time of ImGui (so the manual clock repeats it) and draws the live ones; @p bottomInset is the height left free at
        /// the bottom of the work area
        void Draw(float bottomInset);

        [[nodiscard]] const ToastList& GetList() const {
            return _feed.GetList();
        }

    private:
        ToastFeed _feed;
    };

}  // namespace BECore
