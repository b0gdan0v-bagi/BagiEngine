#pragma once

#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    class ControlService;
    class IUIInputHandler;
    class UIInput;

    /// The pointer id of a click made over the control API (`click_at`, `ui_click`): an id no real mouse has (SDL numbers mice from 1), so a handler that tells pointers apart sees one pointer of its
    /// own
    inline constexpr uint32_t kControlPointerId = 0;

    /// What a click at a point came to
    struct PointClick {
        bool consumed = false;                   ///< a handler took the press
        const IUIInputHandler* taker = nullptr;  ///< who took it; to compare only, it may be gone after the click
    };

    /// Moves the pointer to the output pixel (@p x, @p y), presses @p button and releases it through @p input, as a person does
    PointClick ClickOutputPoint(UIInput& input, float x, float y, uint8_t button, uint32_t which);

    /// Turns the wheel by (@p dx, @p dy) with the pointer at the output pixel (@p x, @p y) through @p input; @p flipped is SDL's `SDL_MOUSEWHEEL_FLIPPED`. The pointer is not moved first:
    /// a wheel carries its own position, and a person's wheel does not move the pointer. @return whether a handler took it
    bool WheelOutputPoint(UIInput& input, float x, float y, float dx, float dy, bool flipped, uint32_t which);

    /**
     * @brief Registers the control API methods `click_at` and `wheel_at`.
     *
     * `click_at`: a click at a point, through `UIInput`, with no element name.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | click_at | {"x", "y": numbers; "space"?: "output" (default) or "scene"; "button"?: "left" (default), "right" or "middle"; "which"?: pointer id, default `kControlPointerId`} | {"x", "y",
     * "outputX", "outputY", "consumed"}; errors `invalid_params`, `no_scene` |
     *
     * "output" is render output pixels, the pixels of a `screenshot`; "scene" is scene units of the active scene, mapped by the fit of its last draw
     * (`step` a frame first; `no_scene` when there is none). A number that is not finite, or outside +-kMaxClickCoordinate, is `invalid_params`, checked before
     * anything is pressed. "consumed" says whether a handler took the press: a scene target (`ClickableComponent`, a game's `PointerTargetComponent`) or a
     * canvas. A press that nobody takes reaches game code as the raw mouse events. The answer comes after the click.
     *
     * | Method | Params | Result |
     * |--------|--------|--------|
     * | wheel_at | {"x", "y": output pixels; "dx"?, "dy"?: numbers, at least one; "flipped"?: bool; "which"?: pointer id} | {"x", "y", "consumed"}; error `invalid_params` |
     *
     * One wheel event through `UIInput::DispatchMouseWheel` at the point, as SDL gives it: `dx` positive to the right, `dy` positive away from the user (up); "flipped" is
     * `SDL_MOUSEWHEEL_FLIPPED`, where SDL reports the values opposite. "consumed" says whether a handler took it (a scrollable document, a canvas); otherwise it
     * reaches game code as the raw `MouseWheelEvent`. Every number is checked before anything is sent.
     *
     * Registering twice replaces the handler.
     */
    void RegisterInputControl(ControlService& service);

}  // namespace BECore
