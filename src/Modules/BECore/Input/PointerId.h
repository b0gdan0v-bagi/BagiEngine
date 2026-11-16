#pragma once

#include <SDL3/SDL_pen.h>
#include <SDL3/SDL_touch.h>
#include <cstdint>

namespace BECore {

    /// The `which` of a finger starts here: kFingerPointerBase + the slot of FingerPointers. High, so it never meets the
    /// instance id of a mouse (SDL numbers those from 1) and stays clear of SDL_TOUCH_MOUSEID and SDL_PEN_MOUSEID
    inline constexpr uint32_t kFingerPointerBase = 0xFFFF0000U;

    /// How many fingers get a pointer id of their own
    inline constexpr uint32_t kMaxFingerPointers = 8;

    [[nodiscard]] constexpr bool IsFingerPointer(uint32_t which) {
        return which >= kFingerPointerBase && which < kFingerPointerBase + kMaxFingerPointers;
    }

    /// A touch screen is the source: a finger, or the mouse event SDL synthesizes from one
    [[nodiscard]] constexpr bool IsTouchPointer(uint32_t which) {
        return which == SDL_TOUCH_MOUSEID || IsFingerPointer(which);
    }

    [[nodiscard]] constexpr bool IsPenPointer(uint32_t which) {
        return which == SDL_PEN_MOUSEID;
    }

}  // namespace BECore
