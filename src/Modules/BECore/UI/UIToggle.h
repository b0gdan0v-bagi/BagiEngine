#pragma once

#include <BECore/UI/UILabel.h>

namespace BECore {

    /**
     * @brief One labeled on/off switch: the caption (the text of a `UILabel`) on the left, a switch at the right end of the rect.
     *
     * **Old content only:** the composed toggle (`UIToggleComponent` + `UISwitchComponent`) is the form; `tools/migrate` step 3 rewrites this one and slice 5 removes the class.
     *
     * `<element type="UIToggle" name="Sound" bind="soundOn" action="SetSound" text="Sound" height="56" .../>`
     *
     * `bind` names a `UIVars` variable; the toggle is *on* when the variable equals `onValue` (default `"true"`) and off otherwise: any other value, an unset variable, an
     * empty `bind` and an empty `onValue` too. `offValue` (default `"false"`) is the value a click asks for when the toggle is on.
     *
     * **The toggle owns no state.** A click emits `ActionEvent(action, value)` with the state the person asks for (`offValue` while the toggle is on, `onValue` while it is off)
     * and changes nothing here: the game answers by setting the variable (or refuses, and the screen keeps showing the truth), the direction of every control (game -> `UIVars` ->
     * widget). The whole rect, caption and switch, is the touch target; a click with an empty `action` or on a `disabled` toggle does nothing.
     *
     * The switch is the only look: a track and a square thumb that sits at the right end when on and at the left end when off, so the state does not rest on color alone. Its size follows
     * the height of the element (named constants below); the caption is laid out in the rest of the width, and `CheckContent()` measures it there, so a caption that would run under the
     * switch is a `TextDoesNotFit` for the validator. `trackColor` (off), `fillColor` (on) and `thumbColor` are derived when left transparent: a translucent `textColor`, a green, the `textColor`.
     * `partShape="Round"` makes the track a pill and the thumb a circle (`UIElement::DrawPart`); the colors and the sizes stay. The element's own plate (`color`) is the box of every element
     * (`box`: rounded corners, border, gradient, shadow), drawn behind the caption and the switch; with the default style it is one rect of `color`.
     */
    class UIToggle : public UILabel {
        BE_CLASS(UIToggle)
    public:
        UIToggle();
        ~UIToggle() override = default;

        BE_REFLECT_FIELD PoolString bind;      ///< the variable that says whether the toggle is on
        BE_REFLECT_FIELD PoolString action;    ///< emitted on a click, with the value to set
        BE_REFLECT_FIELD PoolString onValue;   ///< the value of `bind` that means on; `"true"` unless a file says otherwise
        BE_REFLECT_FIELD PoolString offValue;  ///< the value a click on an on toggle asks for; `"false"` unless a file says otherwise
        BE_REFLECT_FIELD bool disabled = false;
        BE_REFLECT_FIELD UIColor trackColor;                           ///< the track while off; transparent derives it from `textColor`
        BE_REFLECT_FIELD UIColor fillColor;                            ///< the track while on; transparent is a green
        BE_REFLECT_FIELD UIColor thumbColor;                           ///< the thumb; transparent is `textColor`
        BE_REFLECT_FIELD UIPartShape partShape = UIPartShape::Square;  ///< `Round`: a pill track and a round thumb; `Square` (the default) the plain rectangles

        static constexpr float kSwitchHeightRatio = 0.5f;  ///< the track is this share of the element's height, centered
        static constexpr float kSwitchAspect = 2.0f;       ///< the track is this many times wider than high
        static constexpr float kThumbInset = 3.0f;         ///< the air between the track and the thumb, in canvas units
        static constexpr float kCaptionGap = 8.0f;         ///< the air between the caption and the switch

        /// Where the parts are in a rect: the caption area, the track, and the thumb in each state. Pure geometry, so a test and `CheckContent()` use the same numbers
        struct Parts {
            Rect text;
            Rect track;
            Rect thumbOff;  ///< the thumb at the left end
            Rect thumbOn;   ///< the thumb at the right end
        };
        [[nodiscard]] static Parts GetParts(const Rect& area);

        /// The variable equals `onValue`
        [[nodiscard]] bool IsOn() const;

        /// The fill of the track for the state now: on or off, shifted by hover and press, half alpha when `disabled`
        [[nodiscard]] Color GetCurrentTrack() const;

        /// The thumb color: `thumbColor` or `textColor`, half alpha when `disabled`
        [[nodiscard]] Color GetCurrentThumb() const;

        void DrawSelf(IRenderer& renderer) override;
        void OnClick() override;

        /// `disabled` or the group's: the control answers for itself, whatever it sits in
        [[nodiscard]] bool IsDisabled() const override {
            return disabled || groupDisabled;
        }
        /// The control has its own state (hover, press), not that of an ancestor with a clicker
        [[nodiscard]] bool OwnsState() const override {
            return true;
        }

        /// The caption against the area left of the switch, not the whole rect
        [[nodiscard]] UIContentFit CheckContent(float width, float height, float scale) const override;

    protected:
        /// Resolves the colour fields of this control (UIElement::ApplyTheme): `trackColor`, `fillColor`, `thumbColor`
        void ApplyTheme(UIThemes& themes) override;
    };

}  // namespace BECore
