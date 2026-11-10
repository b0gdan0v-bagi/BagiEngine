#pragma once

#include <BECore/UI/UILabel.h>

namespace BECore {

    /**
     * @brief A button: a label on a filled rect that reacts to the pointer and emits an action when it is clicked.
     *
     * `<element type="UIButton" name="Reset" text="Reset" action="Match3Reset" anchor="BottomRight" x="-12" y="-10" width="96" height="48" color="40,90,60,255"/>`
     *
     * A click is a press and its release, both on the button (the canvas follows the press to its release, so dragging off the button cancels it). It emits
     * `InputEvents::ActionEvent(action)`, the same event a ClickableComponent emits, so game code does not tell them apart. The fill follows the state: `color` normally,
     * `hoverColor` under the pointer, `pressedColor` while it is held on the button, `disabledColor` when `disabled`; a color left transparent is derived from `color` (lighter,
     * darker, half transparent). A button is `interactive`, so the layout validator checks that it is big enough to touch (give it 48 units at the reference size).
     * A disabled button still takes the click, it only does nothing with it.
     *
     * `value` goes out with the action, `ActionEvent(action, value)`, so several buttons can share one action and tell a listener which one was pressed (the segments of
     * a choice group). `selected` is a runtime state, set by whoever owns the choice (not read from a file): a selected button is filled from `selectedColor` (lighter
     * than `color` when left transparent) and has a bar of `kSelectedBarHeight` units in `textColor` along its bottom edge, so the selection does not rest on color alone.
     * `groupDisabled` is the same for a whole group: it counts as `disabled`.
     *
     * The state shows in the shape too when the button has a box style (`UIElement::box`; a button with the default one is drawn exactly as above and nothing below applies).
     * `GetCurrentBox` is the one place that decides it:
     * - hover lightens the fill (both gradient ends) by `kHoverBrighten` and the border colour by the same shift;
     * - pressed darkens the fill and the border by `kPressDarken`, shrinks the shadow offset to `kPressShadowOffsetScale` and its blur to `kPressShadowBlurScale` and drops
     *   the highlight;
     * - selected brightens the fill as above, raises the border to `textColor` (at least `kSelectedBorderMin` units wide) and adds a glow of `textColor` when the style has none;
     *   the bar stays, inset by the two bottom radii so it does not leave the rounded corners;
     * - disabled drops the shadow and the glow and keeps `kDisabledAlpha` of the alpha of fill, border and highlight (a disabled button that is also selected keeps the raised
     *   border, faded, and no selected fill).
     * An explicit `hoverColor`, `pressedColor`, `disabledColor` or `selectedColor` replaces the start colour; the gradient end keeps its per-channel offset from the start.
     */
    class UIButton : public UILabel {
        BE_CLASS(UIButton)
    public:
        UIButton();
        ~UIButton() override = default;

        BE_REFLECT_FIELD PoolString action;
        BE_REFLECT_FIELD UIColor hoverColor;
        BE_REFLECT_FIELD UIColor pressedColor;
        BE_REFLECT_FIELD UIColor disabledColor;
        BE_REFLECT_FIELD UIColor selectedColor;
        BE_REFLECT_FIELD PoolString value;       ///< emitted with the action
        BE_REFLECT_FIELD bool disabled = false;  ///< not `enabled`: a vector of factory-made objects writes an `enabled` attribute of its own

        /// The height of the bar at the foot of a selected button, in canvas units
        static constexpr float kSelectedBarHeight = 4.0f;

        /// The shifts of the box states (the colour shifts are `kHoverBrighten` and `kPressDarken`)
        static constexpr float kPressShadowOffsetScale = 0.5f;       ///< a pressed button's shadow keeps this share of its offset
        static constexpr float kPressShadowBlurScale = 2.0f / 3.0f;  ///< a pressed button's shadow keeps this share of its blur
        static constexpr float kSelectedBorderMin = 1.0f;            ///< the least width of the border of a selected button, in canvas units
        static constexpr float kSelectedGlowBlur = 6.0f;             ///< the glow added to a selected button: blur in canvas units
        static constexpr float kSelectedGlowAlpha = 0.4f;            ///< and the share of the alpha of `textColor` it takes

        bool selected = false;       ///< runtime, like `hovered`: drawn as the chosen one
        bool groupDisabled = false;  ///< runtime: the group this button is in is disabled

        void DrawSelf(IRenderer& renderer) override;
        void OnClick() override;

        /// What the button draws its box with in the state it is in now
        struct Look {
            UIBoxStyle box;     ///< the authored box with the state's border, shadow, glow and highlight
            Color fill;         ///< the fill, the start of the gradient
            Color gradientEnd;  ///< the far end of the gradient
        };

        /// The fill for the state the button is in now
        [[nodiscard]] Color GetCurrentFill() const;

        /// The box, the fill and the gradient end for the state the button is in now (pure: it reads the state and changes nothing)
        [[nodiscard]] Look GetCurrentBox() const;
    };

}  // namespace BECore
