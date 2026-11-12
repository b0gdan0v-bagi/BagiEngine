#pragma once

#include <Events/EventBase.h>

namespace BECore {

    /// The viewport was clicked while it was in the Edit input mode (or with the one-off modifier held): the game got nothing, and the
    /// click is for the editor (to pick a node under it). Emitted once per press, in target pixels (the scene's own units, as the game
    /// would have got them); @p button is the ImGui index: 0 left, 1 right, 2 middle.
    struct ViewportEditClickEvent : public EventBase<ViewportEditClickEvent> {
        BE_EVENT(ViewportEditClickEvent)
        BE_REFLECT_FIELD float x = 0.0f;
        BE_REFLECT_FIELD float y = 0.0f;
        BE_REFLECT_FIELD uint8_t button = 0;

        ViewportEditClickEvent() = default;
        ViewportEditClickEvent(float clickX, float clickY, uint8_t clickButton) : x(clickX), y(clickY), button(clickButton) {}
    };

}  // namespace BECore
