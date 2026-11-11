#pragma once

#include <BECore/Math/Vec2.h>

namespace BECore {

    class UIElement;

    /**
     * @brief The rules of the tooltip of an element, with no scene and no renderer: where the plate goes and when it is shown.
     *
     * `UICanvasComponent` holds a `Tracker`, feeds it with what the pointer does and draws the plate where `Place` says (`UIElement::GetTooltipText` is only the text). Times are seconds of game
     * time, any clock that does not go back.
     */
    class UITooltip {
    public:
        static constexpr double kHoverDelay = 0.5;   ///< a mouse resting on the same element this long shows the tooltip
        static constexpr double kLongPress = 0.5;    ///< a touch held on the element this long shows it and is no click
        static constexpr double kTouchLinger = 1.5;  ///< after the finger lifts, a touch tooltip stays this long

        static constexpr float kGap = 8.0f;         ///< between the element and the plate, in canvas units
        static constexpr float kPadding = 10.0f;    ///< between the edge of the plate and the text
        static constexpr float kMaxWidth = 280.0f;  ///< the text wraps at this width
        static constexpr float kFontSize = 18.0f;
        static constexpr Color kPlateColor{24, 26, 32, 235};
        static constexpr Color kTextColor{255, 255, 255, 255};

        /**
         * @brief The plate of a size @p size for the element @p target inside @p bounds (canvas units).
         *
         * Centred under the element with `kGap` between them; when it does not fit below it goes above the element; then it is moved into @p bounds. A plate larger than @p bounds starts
         * at the top-left edge of it. The plate may cover the element when it fits on neither side.
         */
        [[nodiscard]] static Rect Place(const Rect& target, const Vec2& size, const Rect& bounds);

        /**
         * @brief When the tooltip is shown: a small state machine fed with the pointer.
         *
         * Call `Update` whenever the pointer, an element or the clock may have changed (an event, a draw). **Mouse:** shown once the pointer has rested on the same target for `kHoverDelay`;
         * hidden by leaving it and by a press, and a press keeps it hidden until the pointer has left and come back. **Touch:** a finger has no hover, so nothing shows until a press on
         * the target has lasted `kLongPress`: then it is shown and `IsLongPress()` is true (the canvas swallows the click that the release would make); it stays `kTouchLinger` after the
         * release. A short touch is never a long press. Another target, or switching between mouse and touch, starts again.
         */
        class Tracker {
        public:
            /// @p target the element under the pointer that has a tooltip, null for none; @p touch the pointer is a finger; @p pressed a button (a finger: the press that began on @p target) is held
            void Update(const UIElement* target, bool touch, bool pressed, double now);

            /// Forgets everything: nothing is shown
            void Reset();

            /// The tooltip of @p target is on screen as of the last `Update`
            [[nodiscard]] bool IsShown(const UIElement* target) const {
                return _shown && target == _target;
            }

            /// The press that is held, or that the last release ended, lasted `kLongPress` on the target. Cleared by the next press
            [[nodiscard]] bool IsLongPress() const {
                return _longPress;
            }

        private:
            const UIElement* _target = nullptr;
            bool _touch = false;
            bool _pressed = false;
            bool _suppressed = false;  // mouse: a press hid the tooltip, until the pointer leaves the target
            bool _longPress = false;
            bool _shown = false;
            double _since = 0.0;      // when the pointer came to the target, or the press began
            double _lingerEnd = 0.0;  // touch: when the tooltip of a released long press goes
        };
    };

}  // namespace BECore
