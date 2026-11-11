#pragma once

#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITextEffects.h>
#include <BECore/UI/UITextPainter.h>

namespace BECore {

    /**
     * @brief A text in the rect of the element: `<element type="UILabel" text="Score: 0" fontSize="28" textColor="255,255,255,255"/>`.
     *
     * The size is `fontSize` of the style in UI units (24 when it is 0, so a responsive variant can change it); the text is laid out and rasterized at that size times the
     * scale of the canvas, in whole output pixels (`Text/CLAUDE.md`). `overflow` says what happens to a text that does not fit the rect: `Ellipsis` (the default) ends it
     * with "…", `AutoFit` shrinks it down to `minFontSize`, `Clip` cuts the drawing at the rect (and the layout validator reports it, UIContentFit). `color` is a fill behind the text,
     * `textColor` is the text. The face is `font` (a file path), else the nearest face of `fontFamily` at `fontWeight` in the table of `TextConfig.xml`; the validator measures with it. The text may
     * name variables, `text="Score: {score}"`, which follow `CoreManager::GetUIVars()`, and strings, `text="{#menu.start}"`, which follow `CoreManager::GetUIStrings()` and its language. Vertical
     * alignment is the label's (TextLayout has none). `textOutline` and `textShadow` (UITextEffects) paint an outline and a hard shadow round the text of the label and of every control that draws its
     * text through it; they are not measured, and a `Clip` label cuts them at its rect like the text. The text machinery is `UITextPainter`, which `UITextComponent` uses too: a label is
     * the element form of the same text.
     */
    class UILabel : public UIElement {
        BE_CLASS(UILabel)
    public:
        static constexpr float DefaultFontSize = UITextPainter::kDefaultFontSize;

        UILabel() = default;
        ~UILabel() override = default;

        BE_REFLECT_FIELD eastl::string text;
        BE_REFLECT_FIELD PoolString font;            ///< a font path; empty: the face of `fontFamily` and `fontWeight`. A path wins over them
        BE_REFLECT_FIELD PoolString fontFamily;      ///< a family of the `families` table of TextConfig.xml; empty is its default family
        BE_REFLECT_FIELD uint16_t fontWeight = 400;  ///< the CSS weight (400 regular, 700 bold); the nearest face of the family is used
        BE_REFLECT_FIELD UIColor textColor{255, 255, 255, 255};
        BE_REFLECT_FIELD TextAlign align = TextAlign::Left;
        BE_REFLECT_FIELD UIVerticalAlign verticalAlign = UIVerticalAlign::Top;
        BE_REFLECT_FIELD TextOverflow overflow = TextOverflow::Ellipsis;
        BE_REFLECT_FIELD bool wrap = true;
        BE_REFLECT_FIELD float lineSpacing = 1.0f;
        BE_REFLECT_FIELD float minFontSize = 8.0f;   ///< in UI units; the smallest size of AutoFit
        BE_REFLECT_FIELD UITextEffects textEffects;  ///< the outline and the shadow of the text, the attributes `textOutline` and `textShadow`: paint, never part of the layout or the check

        void DrawSelf(IRenderer& renderer) override;
        /// The text against a box of @p width x @p height units. The validator passes the rect; a subclass that draws its text in a smaller area (`DrawText(renderer, area, color)`)
        /// overrides this and calls it with the size of that area, so the check measures what is drawn
        [[nodiscard]] UIContentFit CheckContent(float width, float height, float scale) const override;

        /// The size the shown text takes when it is wrapped at @p maxWidth canvas units, in canvas units at @p scale (output pixels per unit): the width of its widest line and the
        /// height of all lines, laid out as `DrawText` would with a box @p maxWidth wide and as tall as the text needs. `{0, 0}` for an empty text or a font that did not load
        [[nodiscard]] Vec2 MeasureText(float maxWidth, float scale) const;

        /// The text as it is shown: `text` with every `{#key}` replaced by the string of the current language (UIStrings), then every `{name}` by the variable of that
        /// name (UIVars; `{{` and `}}` are literal braces). It is made again only when `text`, a variable or (for a text with a `{#key}`) the string table changed, so a
        /// label that does not change is not laid out again either. A key nobody translated shows as the key and is logged once
        [[nodiscard]] eastl::string_view GetDisplayText() const;

        /// The font size in effect, in UI units: `active.fontSize` (after variants), else DefaultFontSize
        [[nodiscard]] float GetFontSize() const;

        /// The font file the text is measured and drawn with (the path of `font`, else the face of family and weight), empty when it did not load
        [[nodiscard]] PoolString GetResolvedFontPath() const;

        /// The layout of the last draw (empty before the first one)
        [[nodiscard]] const TextLayoutResult& GetLastLayout() const {
            return _painter.GetLastLayout();
        }

        /// How many times the text was laid out: an unchanged label is not laid out again
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _painter.GetLayoutCount();
        }

    protected:
        /// The text that `GetDisplayText()` expands: `text` here. A control that shows another text (a dropdown shows the chosen option's) returns that one, so the authored `text`
        /// stays what a file holds and is never overwritten by what is shown
        [[nodiscard]] virtual const eastl::string& GetSourceText() const {
            return text;
        }

        /// Draws the text only, in the rect, over whatever the subclass drew as the fill
        void DrawText(IRenderer& renderer);

        /// Draws the text only, laid out in @p area (canvas units) instead of the rect, in @p color instead of `textColor`: a control that keeps part of its rect for
        /// something else (the switch of a toggle) draws its caption in the rest. `verticalAlign` is applied inside @p area
        void DrawText(IRenderer& renderer, const Rect& area, const Color& color);

    private:
        [[nodiscard]] UITextParams MakeParams() const;

        mutable UITextPainter _painter;
    };

}  // namespace BECore
