#pragma once

#include <Widgets/Viewport/ViewportEvents.h>

namespace BECore {

    class UIInput;

    /// What a press in the editor viewport is for: Play hands it to the game as the window would, Edit keeps it for the editor
    CORE_ENUM(ViewportInputMode, uint8_t, Play, Edit)

    /// Where one press goes
    CORE_ENUM(ViewportInputTarget, uint8_t, Game, Editor)

    /// Where the press of a button went, kept from the press to its release (None: not held)
    CORE_ENUM(ViewportPressTarget, uint8_t, None, Game, Editor)

    /// What the pointer is: a touch or a pen reaches the game with the `which` of the real window, so a game's touch regime sees it
    CORE_ENUM(ViewportPointerSource, uint8_t, Mouse, Touch, Pen)

    /// @return where a press goes: the mode's target, or the other one while the one-off modifier is held
    [[nodiscard]] ViewportInputTarget ResolveTarget(ViewportInputMode mode, bool oneOffModifier);

    /// A drag on empty space shorter than this (screen units, in x and in y) is a plain click, not a rubber band
    inline constexpr float kBoxSelectMinDrag = 4.0f;

    /// True when the pointer moved from (@p startX, @p startY) to (@p nowX, @p nowY) far enough, in x or in y, for a box selection
    [[nodiscard]] bool IsBoxSelectDrag(float startX, float startY, float nowX, float nowY, float threshold = kBoxSelectMinDrag);

    /// The bits of the `modifiers` field of the viewport events (kSelectionModifierCtrl, kSelectionModifierShift of SceneSelection.h) for the keys held
    [[nodiscard]] uint32_t SelectionModifiersOf(bool ctrl, bool shift);

    /// The mouse buttons the viewport follows, by the ImGui index: 0 left, 1 right, 2 middle
    inline constexpr int kViewportButtonCount = 3;

    /// The pointer of one frame, already in the units of the scene: the position, the delta and the wheel are in target pixels
    /// (the image's top-left corner is (0, 0)), so the widget divides its screen units by the zoom before it fills this
    struct ViewportPointerFrame {
        float x = 0.0f;
        float y = 0.0f;
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        bool hovered = false;  // the pointer is over the image's hit area
        bool down[kViewportButtonCount] = {};
        bool clicked[kViewportButtonCount] = {};    // pressed in this frame
        bool released[kViewportButtonCount] = {};   // released in this frame
        uint8_t clicks[kViewportButtonCount] = {};  // the click count of a press (1 single, 2 double)
        uint32_t modifiers = 0;                     // the selection keys held (SelectionModifiersOf), carried by an edit click
        float wheelX = 0.0f;
        float wheelY = 0.0f;
        ViewportPointerSource source = ViewportPointerSource::Mouse;
    };

    /**
     * @brief Turns the pointer of the viewport image into the game's input, or into an edit click.
     *
     * No ImGui and no renderer: the widget fills a ViewportPointerFrame and calls Forward() once per frame. The events the game
     * gets are the ones the real window gives (UIInput::Dispatch*, so a canvas sees them before game code does), with `which` of a touch
     * or a pen kept.
     *
     * The target of a press is decided at the press and kept to its release, so a press and its release always go the same way,
     * even when the mode flips or the modifier is let go in between. In the Editor target the game gets no press, no motion and no
     * wheel, and the press goes to the sink as an edit click.
     */
    class ViewportPointer {
    public:
        using EditClickSink = eastl::function<void(const ViewportEditClickEvent&)>;

        /// A frame: motion while the pointer is over the image or a game press is held, the presses and the releases of the buttons, the wheel
        void Forward(const ViewportPointerFrame& frame, ViewportInputMode mode, bool oneOffModifier, UIInput& uiInput, const EditClickSink& editClickSink);

        /// Forgets every held press (the game was paused, the widget dies): no release follows
        void Reset();

        /// A press that began in the game and is not released yet
        [[nodiscard]] bool IsGameHeld() const;

    private:
        ViewportPressTarget _held[kViewportButtonCount] = {};
    };

}  // namespace BECore
