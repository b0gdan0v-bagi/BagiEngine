#pragma once

#include <cstdint>

namespace BECore {

    /**
     * @brief What a PointerTargetComponent callback is told about one pointer event.
     *
     * Positions are in the scene's units (the node's rect is in them) and in render output pixels (what UIInput got).
     * The fields a kind of event has no use for stay zero: the deltas are those of a motion, the wheel amounts those of a wheel.
     */
    struct PointerEvent {
        float sceneX = 0.0f;
        float sceneY = 0.0f;
        float outputX = 0.0f;
        float outputY = 0.0f;
        float sceneDx = 0.0f;  ///< motion since the last event of this pointer, scene units
        float sceneDy = 0.0f;
        float wheelX = 0.0f;
        float wheelY = 0.0f;
        uint32_t which = 0;  ///< the pointer: the mouse instance id, or a touch / pen / finger id (PointerId.h)
        uint8_t button = 0;  ///< SDL_BUTTON_*; 0 for a motion and a wheel
        uint8_t clicks = 0;  ///< 1 for a single click, 2 for a double click, ...
        bool touch = false;  ///< a touch screen is the source (IsTouchPointer(which))
    };

}  // namespace BECore
