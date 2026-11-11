#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIComponent.h>
#include <BECore/UI/UIStateLook.h>
#include <BECore/UI/UITextEffects.h>
#include <BECore/UI/UITextPainter.h>

namespace BECore {

    /**
     * @brief A text in the rect of its element, as a component: `<component type="UITextComponent" text="Play" align="Center" verticalAlign="Center"/>`.
     *
     * It has the fields of a `UILabel` under the same attribute names (`text`, `font`, `fontFamily`, `fontWeight`, `textColor`, `align`, `verticalAlign`, `overflow`, `wrap`,
     * `lineSpacing`, `minFontSize`, `textOutline`, `textShadow`) and draws with the same `UITextPainter`, so the two draw the same quads. The size is not a field: it is `fontSize` of the
     * owner's style in UI units (24 when it is 0, so a responsive variant of the element can change it). The text may name `{variables}` and `{#strings}`.
     *
     * **State.** The text takes the colour of the owner's state (a child of a button has the state of the button: `UIElement::GetVisualState`): `disabledTextColor`, `pressedTextColor`,
     * `selectedTextColor`, `hoverTextColor`, in that order of priority; a transparent one leaves `textColor` as it is (a button does not tint its caption). A fainter state colour fades the outline
     * and the shadow of the text by the same share. The check is the text against the rect of the owner: a text that does not fit and cannot shrink is a defect of the layout validator.
     */
    class UITextComponent : public UIComponent {
        BE_CLASS(UITextComponent)
    public:
        UITextComponent() = default;
        ~UITextComponent() override = default;

        BE_REFLECT_FIELD eastl::string text;
        BE_REFLECT_FIELD PoolString font;            ///< a font path; empty: the face of `fontFamily` and `fontWeight`. A path wins over them
        BE_REFLECT_FIELD PoolString fontFamily;      ///< a family of the `families` table of TextConfig.xml; empty is its default family
        BE_REFLECT_FIELD uint16_t fontWeight = 400;  ///< the CSS weight (400 regular, 700 bold); the nearest face of the family is used
        BE_REFLECT_FIELD UIColor textColor{255, 255, 255, 255};
        BE_REFLECT_FIELD UIColor hoverTextColor;     ///< under the pointer; transparent: `textColor`
        BE_REFLECT_FIELD UIColor pressedTextColor;   ///< while pressed; transparent: `textColor`
        BE_REFLECT_FIELD UIColor disabledTextColor;  ///< when the owner is disabled; transparent: `textColor`
        BE_REFLECT_FIELD UIColor selectedTextColor;  ///< when the owner is selected; transparent: `textColor`
        BE_REFLECT_FIELD TextAlign align = TextAlign::Left;
        BE_REFLECT_FIELD UIVerticalAlign verticalAlign = UIVerticalAlign::Top;
        BE_REFLECT_FIELD TextOverflow overflow = TextOverflow::Ellipsis;
        BE_REFLECT_FIELD bool wrap = true;
        BE_REFLECT_FIELD float lineSpacing = 1.0f;
        BE_REFLECT_FIELD float minFontSize = 8.0f;   ///< in UI units; the smallest size of AutoFit
        BE_REFLECT_FIELD UITextEffects textEffects;  ///< the outline and the shadow of the text, the attributes `textOutline` and `textShadow`: paint, never part of the layout or the check

        void DrawSelf(IRenderer& renderer, UIElement& owner) override;
        /// Resolves the colour fields of this component: `textColor`, the four state text colours and the effects
        void ApplyTheme(UIThemes& themes) override;
        [[nodiscard]] UIContentFit CheckContent(const UIElement& owner, float width, float height, float scale) const override;

        /// The colour the text is drawn in for @p state: the state's own colour when it is set, else `textColor`
        [[nodiscard]] Color GetStateColor(const UIStateLook::State& state) const;

        /// The text as it is shown, with its `{#key}` and `{name}` expanded (`UITextPainter::GetDisplayText`)
        [[nodiscard]] eastl::string_view GetDisplayText() const;

        /// The size the shown text takes when wrapped at @p maxWidth canvas units, at the font size of @p owner; see `UITextPainter::Measure`
        [[nodiscard]] Vec2 MeasureText(const UIElement& owner, float maxWidth, float scale) const;

        /// The font file the text is measured and drawn with, empty when it did not load
        [[nodiscard]] PoolString GetResolvedFontPath() const;

        /// How many times the text was laid out: an unchanged text is not laid out again
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _painter.GetLayoutCount();
        }

    private:
        [[nodiscard]] UITextParams MakeParams(float fontSize) const;

        mutable UITextPainter _painter;
    };

}  // namespace BECore
