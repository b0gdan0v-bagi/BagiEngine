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
     * Its fields (`text`, `font`, `fontFamily`, `fontWeight`, `textColor`, `align`, `verticalAlign`, `overflow`, `wrap`, `lineSpacing`, `minFontSize`, `textOutline`,
     * `textShadow`) go to the `UITextPainter`, which lays out, measures, checks and draws the text. A plain text, a caption and a dropdown header are this component. The size is not a field: it is
     * `fontSize` of the owner's style in UI units (24 when it is 0, so a responsive variant of the element can change it). The text may name `{variables}` and `{#strings}`.
     *
     * **State.** The text takes the colour of the owner's state (a child of a button has the state of the button: `UIElement::GetVisualState`): `disabledTextColor`, `pressedTextColor`,
     * `selectedTextColor`, `hoverTextColor`, in that order of priority; a transparent one leaves `textColor` as it is (a button does not tint its caption), except that `fadeWhenDisabled` fades it
     * while the owner is disabled. A fainter state colour fades the outline and the shadow of the text by the same share. The check is the text against the rect of the owner: a text that does not fit
     * and cannot shrink is a defect of the layout validator.
     *
     * **Two hooks of the owner.** The text is drawn in `owner.GetComponentArea()` (the rect unless the owner keeps part of it for something else) and an owner that answers
     * `GetTextOverride()` shows that text instead of `text`: the header of a `UIDropdown` is a text component that draws left of the arrow and shows the chosen caption. For every other
     * element both are the plain case: the rect and `text`.
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
        BE_REFLECT_FIELD UIColor hoverTextColor;         ///< under the pointer; transparent: `textColor`
        BE_REFLECT_FIELD UIColor pressedTextColor;       ///< while pressed; transparent: `textColor`
        BE_REFLECT_FIELD UIColor disabledTextColor;      ///< when the owner is disabled; transparent: `textColor`
        BE_REFLECT_FIELD UIColor selectedTextColor;      ///< when the owner is selected; transparent: `textColor`
        BE_REFLECT_FIELD bool fadeWhenDisabled = false;  ///< true: a disabled owner with no `disabledTextColor` draws `textColor` at `kDisabledAlpha` of its alpha (the caption of a toggle does)
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

        /// The same for the text @p owner shows: its `GetTextOverride()` when it has one, else `text` (a dropdown's header shows the chosen option's caption)
        [[nodiscard]] eastl::string_view GetDisplayText(const UIElement& owner) const;

        /// The size the shown text takes when wrapped at @p maxWidth canvas units, at the font size of @p owner; see `UITextPainter::Measure`
        [[nodiscard]] Vec2 MeasureText(const UIElement& owner, float maxWidth, float scale) const;

        /// The font file the text is measured and drawn with, empty when it did not load
        [[nodiscard]] PoolString GetResolvedFontPath() const;

        /// How many times the text was laid out: an unchanged text is not laid out again
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _painter.GetLayoutCount();
        }

    private:
        /// @p shown is the text to lay out: `text`, or the override of the owner (`ShownText`)
        [[nodiscard]] UITextParams MakeParams(float fontSize, const eastl::string& shown) const;
        [[nodiscard]] static const eastl::string& ShownText(const UIElement& owner, const eastl::string& own);

        mutable UITextPainter _painter;
    };

}  // namespace BECore
