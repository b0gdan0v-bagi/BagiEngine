#pragma once

#include <BECore/Animation/Easing.h>
#include <BECore/UI/UIBoxStyle.h>
#include <BECore/UI/UIColor.h>

namespace BECore {

    /**
     * @brief The rules that turn the state of a control (hover, press, disabled, selected; `checked` is read by the controls, not here) into what it draws. Pure: it reads its arguments and nothing
     * else, so a test pins every number and a button, a composed element and a component all agree on what a state looks like.
     *
     * `ResolveBox` is the look of a box: the shifts of the fill and the gradient end, the border, shadow, glow and highlight (a box with no effect keeps only the fill shifts).
     * `ResolveTint` is the tint of a picture or a text colour with the same shifts. The colour of a state that is given replaces the derived one; the derived ones are lighter
     * under the pointer (`kHoverBrighten`), darker while pressed (`kPressDarken`) and half transparent when disabled (`kDisabledAlpha`).
     */
    class UIStateLook {
    public:
        /// What the pointer and the owner say about the control now. `pressed` is a press that is held **and** still over the control: a press that moved off is no longer shown as pressed
        /// (letting go there does nothing), and whoever fills this in has made that decision (`UIElement::GetVisualState`)
        struct State {
            bool hovered = false;
            bool pressed = false;
            bool disabled = false;
            bool selected = false;
            /// A toggle that is on. Only the components of the toggle itself read it (the switch puts its thumb at the on end); no `ResolveBox`, `ResolveTint` or text colour does, so the
            /// plate of a toggle does not light up for being on. A state owner hands it down to its children like `hovered`
            bool checked = false;
        };

        /// The authored fill of each state; a transparent colour means "derive it from the plain one"
        struct Colors {
            Color hover;
            Color pressed;
            Color disabled;
            Color selected;
        };

        /// The authored tint of each state of a picture; a transparent colour means "derive it from the plain one". A selected picture is left alone
        struct Tints {
            Color hover;
            Color pressed;
            Color disabled;
        };

        /// What a box is drawn with in a state
        struct Look {
            UIBoxStyle box;     ///< the authored box with the state's border, shadow, glow and highlight
            Color fill;         ///< the fill, the start of the gradient
            Color gradientEnd;  ///< the far end of the gradient
        };

        /// The ease of the smooth change between two states (`UIBlend`, `UISettings::stateTransition`)
        static constexpr EaseType kTransitionEase = EaseType::QuadOut;

        /// The height of the bar at the foot of a selected control, in canvas units
        static constexpr float kSelectedBarHeight = 4.0f;

        /// The shifts of the box states (the colour shifts are `kHoverBrighten` and `kPressDarken`)
        static constexpr float kPressShadowOffsetScale = 0.5f;       ///< a pressed control's shadow keeps this share of its offset
        static constexpr float kPressShadowBlurScale = 2.0f / 3.0f;  ///< a pressed control's shadow keeps this share of its blur
        static constexpr float kSelectedBorderMin = 1.0f;            ///< the least width of the border of a selected control, in canvas units
        static constexpr float kSelectedGlowBlur = 6.0f;             ///< the glow added to a selected control: blur in canvas units
        static constexpr float kSelectedGlowAlpha = 0.4f;            ///< and the share of the alpha of the accent it takes
        static constexpr int kSelectedBrighten = 56;                 ///< more than kHoverBrighten: a selected control under the pointer is still told from a hovered one

        /**
         * The box, the fill and the gradient end of @p box filled with @p color in @p state. @p accent is the colour of the selection (a styled box gets it as its border and, when it
         * has no glow, as a glow); a control passes its text colour. An explicit colour of @p stateColors replaces the start colour, and the gradient end keeps its per-channel offset
         * from it. Disabled wins over the rest: it drops the shadow and the glow, fades fill, border and highlight to `kDisabledAlpha` and keeps the raised border of a selected
         * control, faded, but not its brighter fill.
         */
        [[nodiscard]] static Look ResolveBox(const UIBoxStyle& box, const Color& color, const Colors& stateColors, const State& state, const Color& accent);

        /**
         * The look @p t of the way from @p from to @p to (not clamped, like `BECore::Lerp`, so an overshooting ease reaches its bulge; 0 gives @p from and 1 @p to exactly, NaN gives @p from). The
         * numbers and colours blend: the fill, the gradient end, the box's border (width and colour), the gradient colour, the highlight, and the offset, blur, spread and colour of the shadow and the
         * glow. What cannot be between two values comes from @p to: the corner radii, the gradient direction and the theme token names. A colour that has no alpha at one end takes the channels of the
         * other end, so a shadow that appears fades in as itself and does not pass through black.
         */
        [[nodiscard]] static Look Lerp(const Look& from, const Look& to, float t);

        /// @p base in @p state: the explicit tint of the state, else the derived one. Disabled wins over pressed, pressed over hovered
        [[nodiscard]] static Color ResolveTint(const Color& base, const Tints& tints, const State& state);

        /// The bar of a selected control along the bottom edge of @p rect: as high as `kSelectedBarHeight` allows, inset by the two bottom radii of @p box so it does not leave the
        /// rounded corners. Empty for an empty rect or when the radii leave no straight part
        [[nodiscard]] static Rect SelectedBar(const Rect& rect, const UIBoxStyle& box);
    };

}  // namespace BECore
