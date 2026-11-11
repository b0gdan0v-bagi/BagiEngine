#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Text/Font.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/Text/TextLayoutCache.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITextEffects.h>

namespace BECore {

    /// Where the text sits in the height of its rect when it does not fill it
    CORE_ENUM(UIVerticalAlign, uint8_t, Top, Center, Bottom)

    /// What the painter reads of a text: the fields of a `UILabel` or of a `UITextComponent`, taken as they are now. A view, never kept
    struct UITextParams {
        eastl::string_view text;  ///< the source text, with its `{#key}` and `{name}` (the painter expands them)
        PoolString font;
        PoolString fontFamily;
        uint16_t fontWeight = 400;
        float fontSize = 0.0f;  ///< UI units; 0 is `UITextPainter::kDefaultFontSize`
        TextAlign align = TextAlign::Left;
        UIVerticalAlign verticalAlign = UIVerticalAlign::Top;
        TextOverflow overflow = TextOverflow::Ellipsis;
        bool wrap = true;
        float lineSpacing = 1.0f;
        float minFontSize = 8.0f;  ///< UI units
        UITextEffects effects;
    };

    /**
     * @brief The text machinery of a label and of a text component: the placeholder expansion, the font, the layout cache, the drawing, the measure and the fit check.
     *
     * Not reflected and kept by its owner, one per text, because it caches: the expanded text (made again only when the source, a variable or, for a text with a `{#key}`, the string
     * table changed), the font handle (loaded again when the path changes; a family nobody knows and a font that does not load are logged once) and the layout (made again only when
     * the text or what shapes it changed, so a steady screen costs no layout). It knows nothing of the element it serves: every call takes the fields as a `UITextParams`.
     */
    class UITextPainter {
    public:
        static constexpr float kDefaultFontSize = 24.0f;

        /// @p fontSize, or `kDefaultFontSize` when it is 0
        [[nodiscard]] static float EffectiveFontSize(float fontSize);

        /// The text as it is shown: @p source with every `{#key}` replaced by the string of the current language (UIStrings), then every `{name}` by the variable of that name
        /// (UIVars; `{{` and `}}` are literal braces). A key nobody translated shows as the key and is logged once. The view is valid until the next call
        [[nodiscard]] eastl::string_view GetDisplayText(eastl::string_view source);

        /// The font file the text is measured and drawn with (the path of `font`, else the face of family and weight), empty when it did not load
        [[nodiscard]] PoolString GetResolvedFontPath(const UITextParams& params);

        /// Draws the text laid out in @p area (canvas units) in @p color. @p fullColor is the colour of the text when it is not faded: a @p color that is fainter fades the outline and the
        /// shadow by the same share. Nothing for an empty text or area, a transparent @p color or a font that did not load. `verticalAlign` is applied inside @p area
        void Draw(IRenderer& renderer, const UITextParams& params, const Rect& area, const Color& color, const Color& fullColor);

        /// The size the text takes when wrapped at @p maxWidth canvas units, in canvas units at @p scale (output pixels per unit): its widest line and the height of all lines. `{0, 0}`
        /// for an empty text or a font that did not load
        [[nodiscard]] Vec2 Measure(const UITextParams& params, float maxWidth, float scale);

        /// The text against a box of @p width x @p height units, for the layout validator
        [[nodiscard]] UIContentFit Check(const UITextParams& params, float width, float height, float scale);

        /// The layout of the last draw (empty before the first one)
        [[nodiscard]] const TextLayoutResult& GetLastLayout() const {
            return _layoutCache.GetLast();
        }

        /// How many times the text was laid out
        [[nodiscard]] uint32_t GetLayoutCount() const {
            return _layoutCache.GetLayoutCount();
        }

    private:
        [[nodiscard]] Font* ResolveFont(const UITextParams& params);

        ResourceHandle<Font> _fontHandle;
        PoolString _fontPath;      // the path _fontHandle was loaded from, to notice a changed font, family or weight
        FontRequest _fontRequest;  // the request of the last resolve: an unknown family is logged when it changes, not on every frame
        TextLayoutCache _layoutCache;
        eastl::string _expandedSource;  // the source that _expanded was made from
        eastl::string _expanded;
        uint64_t _expandedVersion = 0;         // the version of UIVars it was made at
        uint64_t _expandedStringsVersion = 0;  // the version of UIStrings it was made at, read only when _expandedUsesStrings
        bool _expandedUsesStrings = false;     // the text holds a `{#key}`
    };

}  // namespace BECore
