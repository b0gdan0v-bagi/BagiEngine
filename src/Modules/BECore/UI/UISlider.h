#pragma once

#include <BECore/UI/UIElement.h>

namespace BECore {

    /**
     * @brief A horizontal slider for a number: a thin track with a square thumb on it. A press or a drag asks for a value; the game answers by setting the variable.
     *
     * `<element type="UISlider" name="Volume" bind="volume" action="SetVolume" minValue="0" maxValue="1" step="0.1" height="56" .../>`
     *
     * `bind` names a `UIVars` variable that holds the number; `minValue` (0) and `maxValue` (1) are the ends of the range and `step` (0 = continuous) the grid a value is snapped to.
     * The thumb stands at `fraction` of the travel, read from the variable (`UIText::ValueInRange`, `FractionInRange`: an unset or malformed variable is the minimum, a value outside the
     * range is clamped, an empty or reversed range is a fraction of 0). **The slider owns no value**: nothing it does changes the variable, and a slider whose game does not answer stays
     * where it was (game -> `UIVars` -> widget).
     *
     * **Input.** The slider is `interactive`. A press anywhere on its rect emits `ActionEvent(action, valueText)` with the value under the pointer, and the canvas then follows the press
     * to its release (`UIElement::OnPress` / `OnDrag`), also outside the rect: every motion emits again, but only when the text of the value changed, so a drag inside one step is
     * silent. The release emits nothing, and the slider has no `OnClick`, so one gesture never fires a second action. A `disabled` slider takes the press (it does not fall through to what
     * is below) and does nothing; so does one with an empty `action`.
     *
     * **The value under a point.** The thumb is a square of the element's height (a slider of 56 units has a 56-unit thumb, which the validator's touch check already holds) and never
     * leaves the rect, so the travel of its centre is the rect narrowed by half a thumb on both sides. The value is the position in the travel, clamped (the edges and everything within
     * half a thumb of them are the minimum and the maximum), then snapped to `step` from `minValue`; the maximum is also a place to snap to when the range is not a whole number of steps.
     * `FormatValue` writes it short and exact: `0.3`, never `0.30000001`; `7`, never `7.0`.
     *
     * The track (`trackColor`), the part before the thumb (`fillColor`) and the thumb (`thumbColor`) are derived when left transparent. A `disabled` slider is drawn at half alpha. The
     * thumb is drawn lighter under the pointer and darker while the press is held (wherever the pointer is).
     * `partShape="Round"` makes the track and the filled part pills and the thumb a circle (`UIElement::DrawPart`); the filled part is a smaller pill while it is shorter than the track
     * is high. The geometry (`GetParts`), the touch area and the hit test do not change.
     */
    class UISlider : public UIElement {
        BE_CLASS(UISlider)
    public:
        UISlider();
        ~UISlider() override = default;

        BE_REFLECT_FIELD PoolString bind;    ///< the variable that holds the number
        BE_REFLECT_FIELD PoolString action;  ///< emitted on a press and on every change of the value while the press is held
        BE_REFLECT_FIELD float minValue = 0.0f;
        BE_REFLECT_FIELD float maxValue = 1.0f;
        BE_REFLECT_FIELD float step = 0.0f;  ///< 0 (or less) = continuous
        BE_REFLECT_FIELD bool disabled = false;
        BE_REFLECT_FIELD UIColor trackColor;                           ///< the whole track; transparent is a translucent white
        BE_REFLECT_FIELD UIColor fillColor;                            ///< the track before the thumb; transparent is a green
        BE_REFLECT_FIELD UIColor thumbColor;                           ///< the thumb; transparent is a near white
        BE_REFLECT_FIELD UIPartShape partShape = UIPartShape::Square;  ///< `Round`: a pill track and fill and a round thumb; `Square` (the default) the plain rectangles

        static constexpr float kTrackHeight = 6.0f;                 ///< the track is this high in canvas units (less for a slider lower than that), centered
        static constexpr Color kThumbFallback{240, 240, 240, 255};  ///< the thumb when `thumbColor` is left transparent

        /// Where the parts are in a rect for a value at @p fraction (0..1). Pure geometry, so a test and the drawing use the same numbers
        struct Parts {
            Rect track;                ///< the whole track
            Rect filled;               ///< the part of it before the thumb's centre
            Rect thumb;                ///< a square of the height of the rect (the width when that is smaller), inside the rect
            float travelStart = 0.0f;  ///< the x of the thumb's centre at the minimum
            float travelEnd = 0.0f;    ///< ... and at the maximum
        };
        [[nodiscard]] static Parts GetParts(const Rect& area, float fraction);

        /// The variable as a number in `minValue..maxValue`, read now: the minimum when it is unset or malformed
        [[nodiscard]] float GetValue() const;

        /// 0..1: where the thumb stands, read from the variable now
        [[nodiscard]] float GetFraction() const;

        /// The value a pointer at @p canvasX asks for: clamped to the travel, snapped to `step`. `minValue` for an empty or reversed range
        [[nodiscard]] float ValueAt(float canvasX) const;

        /// A value as text, short and exact: at most six significant digits, no trailing zeros, no "-0" ("0.3", "7", "35", "-1.5")
        [[nodiscard]] static eastl::string FormatValue(float value);

        /// The value the last press or drag emitted (empty before the first, and when nothing was emitted: disabled, no action)
        [[nodiscard]] PoolString GetLastEmittedValue() const {
            return _lastEmitted;
        }

        /// The colors in effect: the file's, or the derived ones, shifted by the pointer and halved when `disabled`
        [[nodiscard]] Color GetCurrentTrack() const;
        [[nodiscard]] Color GetCurrentFill() const;
        [[nodiscard]] Color GetCurrentThumb() const;

        void DrawSelf(IRenderer& renderer) override;
        void OnPress(float x, float y) override;
        void OnDrag(float x, float y) override;

    protected:
        /// Resolves the colour fields of this control (UIElement::ApplyTheme): `trackColor`, `fillColor`, `thumbColor`
        void ApplyTheme(UIThemes& themes) override;

    private:
        /// Emits the value at @p x unless it is the one emitted last
        void Follow(float x);

        PoolString _lastEmitted;
    };

}  // namespace BECore
