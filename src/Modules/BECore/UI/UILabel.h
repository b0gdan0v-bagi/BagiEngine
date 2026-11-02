#pragma once

#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/TextLayoutCache.h>
#include <BECore/UI/UIElement.h>

namespace BECore {

    /// Where the text sits in the height of its rect when it does not fill it
    CORE_ENUM(UIVerticalAlign, uint8_t, Top, Center, Bottom)

    /**
     * @brief A text in the rect of the element: `<element type="UILabel" text="Score: 0" fontSize="28" textColor="255,255,255,255"/>`.
     *
     * The size is `fontSize` of the style in UI units (24 when it is 0, so a responsive variant can change it); the text is laid out and rasterized at that size times the
     * scale of the canvas, in whole output pixels (`Text/CLAUDE.md`). `overflow` says what happens to a text that does not fit the rect: `Ellipsis` (the default) ends it
     * with "…", `AutoFit` shrinks it down to `minFontSize`, `Clip` cuts the drawing at the rect (and the layout validator reports it, UIContentFit). `color` is a fill behind the text,
     * `textColor` is the text. The text may name variables, `text="Score: {score}"`, which follow `CoreManager::GetUIVars()`. Vertical alignment is the label's (TextLayout has none).
     */
    class UILabel : public UIElement {
        BE_CLASS(UILabel)
    public:
        static constexpr float DefaultFontSize = 24.0f;

        UILabel() = default;
        ~UILabel() override = default;

        BE_REFLECT_FIELD eastl::string text;
        BE_REFLECT_FIELD PoolString font;  ///< a font path; empty is the default font of TextConfig.xml
        BE_REFLECT_FIELD UIColor textColor{255, 255, 255, 255};
        BE_REFLECT_FIELD TextAlign align = TextAlign::Left;
        BE_REFLECT_FIELD UIVerticalAlign verticalAlign = UIVerticalAlign::Top;
        BE_REFLECT_FIELD TextOverflow overflow = TextOverflow::Ellipsis;
        BE_REFLECT_FIELD bool wrap = true;
        BE_REFLECT_FIELD float lineSpacing = 1.0f;
        BE_REFLECT_FIELD float minFontSize = 8.0f;  ///< in UI units; the smallest size of AutoFit

        void DrawSelf(IRenderer& renderer) override;
        [[nodiscard]] UIContentFit CheckContent(float width, float height, float scale) const override;

        /// The text as it is shown: `text` with every `{name}` replaced by the variable of that name (UIVars; `{{` and `}}` are literal braces). It is expanded
        /// again only when `text` or a variable changed, so a label that does not change is not laid out again either
        [[nodiscard]] eastl::string_view GetDisplayText() const;

        /// The font size in effect, in UI units: `active.fontSize` (after variants), else DefaultFontSize
        [[nodiscard]] float GetFontSize() const;

        /// The layout of the last draw (empty before the first one)
        [[nodiscard]] const TextLayoutResult& GetLastLayout() const {
            return _layoutCache.GetLast();
        }

        /// How many times the text was laid out: an unchanged label is not laid out again
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _layoutCache.GetLayoutCount();
        }

    protected:
        /// Draws the text only, in the rect, over whatever the subclass drew as the fill
        void DrawText(IRenderer& renderer);

    private:
        [[nodiscard]] TextLayoutOptions MakeOptions(float width, float height, float scale) const;
        [[nodiscard]] Font* ResolveFont() const;

        mutable ResourceHandle<Font> _fontHandle;
        mutable PoolString _fontPath;  // the path _fontHandle was loaded from, to notice a changed `font`
        mutable TextLayoutCache _layoutCache;
        mutable eastl::string _expandedSource;  // the `text` that _expanded was made from
        mutable eastl::string _expanded;
        mutable uint64_t _expandedVersion = 0;  // the version of UIVars it was made at
    };

}  // namespace BECore
