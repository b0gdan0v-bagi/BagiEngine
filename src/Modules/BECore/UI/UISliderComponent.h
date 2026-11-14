#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>

namespace BECore {

    /**
     * @brief A horizontal slider for a number, as a component: a thin track with a square thumb on it. A press or a drag asks for a value; the game answers by setting the variable.
     *
     * `<element type="UIElement" name="Volume" height="56"><components><component type="UIInputClicker" stateLook="false"/><component type="UISliderComponent" bind="volume" action="SetVolume"
     * minValue="0" maxValue="1" step="0.1"/></components></element>`
     *
     * The clicker beside it makes the element `disabled`-able (one source of it), gives the pointer state to the thumb and, with `stateLook="false"`, leaves the plate of the element still
     * (a slider's plate never changed with the pointer). Its own `action` stays empty: this component emits on a press and a drag, which the clicker's fixed `(action, value)` on a click cannot.
     * It still makes the element interactive by itself, so a forgotten clicker leaves a working slider, only one that cannot be disabled.
     *
     * `bind` names a `UIVars` variable that holds the number; `minValue` (0) and `maxValue` (1) are the ends of the range and `step` (0 = continuous) the grid a value is snapped to.
     * The thumb stands at `GetFraction()` of the travel, read from the variable (`UIText::ValueInRange`, `FractionInRange`: an unset or malformed variable is the minimum, a value outside the
     * range is clamped, an empty or reversed range is a fraction of 0). **The slider owns no value**: nothing it does changes the variable (game -> `UIVars` -> widget).
     *
     * **Input.** A press anywhere on the rect emits `ActionEvent(action, valueText)` with the value under the pointer, and the canvas then follows the press to its release
     * (`OnPress` / `OnDrag`), also outside the rect: every motion emits again, but only when the text of the value changed, so a drag inside one step is silent. The release emits nothing, and
     * there is no `OnClick`, so one gesture never fires a second action. A disabled owner takes the press (it does not fall through to what is below) and does nothing; so does a slider with an
     * empty `action`.
     *
     * **The value under a point.** The thumb is a square of the height of the rect and never leaves it, so the travel of its centre is the rect narrowed by half a thumb on both sides. The value
     * is the position in the travel, clamped, then snapped to `step` from `minValue`; the maximum is also a place to snap to when the range is not a whole number of steps. `FormatValue`
     * writes it short and exact: `0.3`, never `0.30000001`; `7`, never `7.0`.
     *
     * The track (`trackColor`), the part before the thumb (`fillColor`) and the thumb (`thumbColor`) are derived when left transparent. A disabled slider is drawn at half alpha. The thumb is
     * lighter under the pointer and darker while the press is held (wherever the pointer is). `partShape="Round"` makes the track and the filled part pills and the thumb a circle
     * (`UIElement::DrawPart`).
     */
    class UISliderComponent : public UIComponent {
        BE_CLASS(UISliderComponent)
    public:
        UISliderComponent() = default;
        ~UISliderComponent() override = default;

        BE_REFLECT_FIELD PoolString bind;    ///< the variable that holds the number
        BE_REFLECT_FIELD PoolString action;  ///< emitted on a press and on every change of the value while the press is held
        BE_REFLECT_FIELD float minValue = 0.0f;
        BE_REFLECT_FIELD float maxValue = 1.0f;
        BE_REFLECT_FIELD float step = 0.0f;                            ///< 0 (or less) = continuous
        BE_REFLECT_FIELD UIColor trackColor;                           ///< the whole track; transparent is a translucent white
        BE_REFLECT_FIELD UIColor fillColor;                            ///< the track before the thumb; transparent is a green
        BE_REFLECT_FIELD UIColor thumbColor;                           ///< the thumb; transparent is a near white
        BE_REFLECT_FIELD UIPartShape partShape = UIPartShape::Square;  ///< `Round`: a pill track and fill and a round thumb; `Square` (the default) the plain rectangles

        static constexpr float kTrackHeight = 6.0f;                 ///< the track is this high in canvas units (less for a slider lower than that), centred
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

        /// The value a pointer at @p canvasX asks for in a slider laid out in @p area: clamped to the travel, snapped to @p step. @p minValue for an empty or reversed range
        [[nodiscard]] static float ValueAtX(const Rect& area, float minValue, float maxValue, float step, float canvasX);

        /// A value as text, short and exact: at most six significant digits, no trailing zeros, no "-0" ("0.3", "7", "35", "-1.5")
        [[nodiscard]] static String FormatValue(float value);

        /// The variable as a number in `minValue..maxValue`, read now: the minimum when it is unset or malformed
        [[nodiscard]] float GetValue() const;

        /// 0..1: where the thumb stands, read from the variable now
        [[nodiscard]] float GetFraction() const;

        /// The value a pointer at @p canvasX asks for in the rect of @p owner
        [[nodiscard]] float ValueAt(const UIElement& owner, float canvasX) const;

        /// The value the last press or drag emitted (empty before the first, and when nothing was emitted: disabled, no action)
        [[nodiscard]] PoolString GetLastEmittedValue() const {
            return _lastEmitted;
        }

        /// The colours in effect for @p owner: the file's, or the derived ones, halved when the owner is disabled. The thumb is also shifted by the pointer: lighter while hovered, darker
        /// while `pressed`
        [[nodiscard]] Color GetCurrentTrack(const UIElement& owner) const;
        [[nodiscard]] Color GetCurrentFill(const UIElement& owner) const;
        [[nodiscard]] Color GetCurrentThumb(const UIElement& owner) const;

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        void OnPress(UIElement& owner, float x, float y) override;
        void OnDrag(UIElement& owner, float x, float y) override;
        [[nodiscard]] bool MakesInteractive() const override;
        /// Resolves the colour fields of this component: `trackColor`, `fillColor`, `thumbColor`
        void ApplyTheme(UIThemes& themes) override;

    private:
        /// Emits the value at @p x unless it is the one emitted last
        void Follow(const UIElement& owner, float x);

        PoolString _lastEmitted;
    };

}  // namespace BECore
