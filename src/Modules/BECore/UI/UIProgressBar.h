#pragma once

#include <BECore/UI/UIElement.h>

namespace BECore {

    /**
     * @brief A bar that shows how far a number has come: the filled part is a share of the rect's width, from the left. Read-only.
     *
     * `<element type="UIProgressBar" name="Loading" bind="loadProgress" minValue="0" maxValue="100" height="24" .../>`
     *
     * `bind` names a `UIVars` variable that holds a number; `minValue` (default 0) and `maxValue` (default 1) are the ends of the range. The bar shows
     * `fraction = clamp((value - minValue) / (maxValue - minValue), 0, 1)`: a value below the range is an empty bar, above it a full one. An unset variable, text that is not a
     * number ("abc", "nan", "inf"), an empty or reversed range (`maxValue <= minValue`) and a number that is not finite are a fraction of 0: never a division by zero, never a rect
     * outside the bar (`UIText::FractionInRange`, shared with the slider). It reads the variable when it is asked, not only when it is drawn, so the control API sees the value of
     * this moment.
     *
     * The bar takes no input (not `interactive`, `blocksInput` false), so a click on it goes to what is below. The whole rect is drawn as the track (`trackColor`), the filled part over it
     * (`fillColor`); a transparent one is derived from named constants: a translucent white track and the green of the toggle. There is no text on the bar: a `UILabel` beside it with
     * `{var}` says the number. `partShape="Round"` makes the track and the filled part pills (`UIElement::DrawPart`); a fill shorter than the bar is high is a smaller pill.
     */
    class UIProgressBar : public UIElement {
        BE_CLASS(UIProgressBar)
    public:
        UIProgressBar() = default;
        ~UIProgressBar() override = default;

        BE_REFLECT_FIELD PoolString bind;  ///< the variable that holds the number
        BE_REFLECT_FIELD float minValue = 0.0f;
        BE_REFLECT_FIELD float maxValue = 1.0f;
        BE_REFLECT_FIELD UIColor trackColor;                           ///< the whole bar; transparent is a translucent white
        BE_REFLECT_FIELD UIColor fillColor;                            ///< the filled part; transparent is a green
        BE_REFLECT_FIELD UIPartShape partShape = UIPartShape::Square;  ///< `Round`: a pill track and fill; `Square` (the default) the plain rectangles

        static constexpr Color kTrackFallback{255, 255, 255, 60};  ///< the track when `trackColor` is left transparent

        /// 0..1: how much of the bar is filled, read from the variable now (the rules are in the class comment)
        [[nodiscard]] float GetFraction() const;

        /// The colors in effect: the file's, or the derived ones
        [[nodiscard]] Color GetCurrentTrack() const;
        [[nodiscard]] Color GetCurrentFill() const;

        void DrawSelf(IRenderer& renderer) override;

    protected:
        /// Resolves the colour fields of this control (UIElement::ApplyTheme): `trackColor`, `fillColor`
        void ApplyTheme(UIThemes& themes) override;
    };

}  // namespace BECore
