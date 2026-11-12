#pragma once

#include <BECore/UI/UIBoxMesh.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UIStateLook.h>

namespace BECore {

    /**
     * @brief The look of a toggle's switch: a track and a thumb that sits at the right end when on and at the left end when off, so the state does not rest on colour alone.
     * `<component type="UISwitchComponent" trackColor="60,60,60,255" fillColor="60,160,90,255" thumbColor="255,255,255,255" partShape="Round"/>`; it is the component of the switch child
     * of a toggle row (`UIToggleComponent`).
     *
     * It draws in the rect of its own element, from the state of its owner: the owner is a child of the row and has none of its own, so `UIElement::GetVisualState` gives it the row's
     * (hover, press, disabled, `checked`). `trackColor` is the track while off, `fillColor` while on, `thumbColor` the thumb; left transparent they derive from a **white caption** (the off track
     * is white at `kOffTrackAlpha`, the thumb white, the on track `kFillFallback`), because the switch cannot see the caption; a file with another caption colour says them. The track is
     * lighter under the pointer, darker pressed and half transparent when disabled. `partShape="Round"` makes the track a pill and the thumb a circle (`UIElement::DrawPart`).
     *
     * Sizes follow the rect: the track is `kSwitchHeightRatio` of its height, centred, `kSwitchAspect` times wider than high, with the air above and below it at the right end too. A switch
     * element is `height="100%" aspectRatio="1.25"` (`kAreaAspect`) so that the air at the right end is a quarter of the height and the track starts at the left edge of the element.
     */
    class UISwitchComponent : public UIComponent {
        BE_CLASS(UISwitchComponent)
    public:
        UISwitchComponent() = default;
        ~UISwitchComponent() override = default;

        BE_REFLECT_FIELD UIColor trackColor;                           ///< the track while off; transparent is a translucent white
        BE_REFLECT_FIELD UIColor fillColor;                            ///< the track while on; transparent is a green
        BE_REFLECT_FIELD UIColor thumbColor;                           ///< the thumb; transparent is white
        BE_REFLECT_FIELD UIPartShape partShape = UIPartShape::Square;  ///< `Round`: a pill track and a round thumb; `Square` (the default) the plain rectangles

        static constexpr float kSwitchHeightRatio = 0.5f;  ///< the track is this share of the height of the rect, centred
        static constexpr float kSwitchAspect = 2.0f;       ///< the track is this many times wider than high
        static constexpr float kThumbInset = 3.0f;         ///< the air between the track and the thumb, in canvas units
        /// The width of the switch element over its height: the track (`kSwitchAspect` x `kSwitchHeightRatio` = 1 height) and the air at the right end, which is a quarter of the height (as
        /// much as above and below the track)
        static constexpr float kAreaAspect = 1.25f;
        static constexpr float kCaptionGap = 8.0f;      ///< the air between a toggle's caption and its switch: the spacing of the row (the HTML compiler writes it)
        static constexpr float kOffTrackAlpha = 0.35f;  ///< of the white the off track derives from: a track that is there without shouting

        /// Where the parts are in a rect: the track, and the thumb in each state. Pure geometry, so a test and the drawing use the same numbers
        struct Parts {
            Rect track;
            Rect thumbOff;  ///< the thumb at the left end
            Rect thumbOn;   ///< the thumb at the right end
        };
        [[nodiscard]] static Parts GetParts(const Rect& area);

        /// The fill of the track for @p state: on (`checked`) or off, shifted by hover and press, half alpha when disabled
        [[nodiscard]] Color GetCurrentTrack(const UIStateLook::State& state) const;

        /// The thumb colour for @p state: `thumbColor` or white, half alpha when disabled
        [[nodiscard]] Color GetCurrentThumb(const UIStateLook::State& state) const;

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        /// Resolves the colour fields of this component: `trackColor`, `fillColor`, `thumbColor`
        void ApplyTheme(UIThemes& themes) override;
    };

}  // namespace BECore
