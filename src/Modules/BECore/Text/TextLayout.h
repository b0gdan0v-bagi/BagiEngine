#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    class Font;

    CORE_ENUM(TextAlign, uint8_t, Left, Center, Right)

    /// What happens to text that does not fit the box.
    /// Clip: nothing is cut here; TextLayoutResult::overflowed tells the drawer to clip. Ellipsis: the text is cut and ends with
    /// "…". AutoFit: the font shrinks, down to minFontSize, until it fits (and gets an ellipsis if even that is too big).
    CORE_ENUM(TextOverflow, uint8_t, Clip, Ellipsis, AutoFit)

    struct TextLayoutOptions {
        float fontSize = 16.0f;  ///< pixels, the em square of the font (see Font)
        float maxWidth = 0.0f;   ///< the width of the box; 0 is unlimited: no wrap, no ellipsis on the width
        float maxHeight = 0.0f;  ///< the height of the box; 0 is unlimited
        TextAlign align = TextAlign::Left;
        float lineSpacing = 1.0f;  ///< multiplies the line height of the font
        bool wrap = true;          ///< break lines at spaces (and inside a word that is wider than the box) when maxWidth > 0
        TextOverflow overflow = TextOverflow::Clip;
        float minFontSize = 8.0f;
    };

    /// The pen position of one glyph, relative to the top-left corner of the text box. y is the baseline of its line.
    struct PositionedGlyph {
        char32_t codePoint = 0;
        uint32_t glyph = 0;
        float x = 0.0f;
        float y = 0.0f;
    };

    struct TextLine {
        uint32_t firstGlyph = 0;
        uint32_t glyphCount = 0;
        float x = 0.0f;      ///< where the line starts (the alignment offset)
        float width = 0.0f;  ///< up to the end of the last glyph that has ink, trailing spaces left out
        float baseline = 0.0f;
    };

    struct TextLayoutResult {
        Vector<PositionedGlyph> glyphs;
        Vector<TextLine> lines;
        float width = 0.0f;  ///< of the widest line
        float height = 0.0f;
        float fontSize = 0.0f;  ///< the size that was used; smaller than asked only after AutoFit
        float lineHeight = 0.0f;
        bool truncated = false;   ///< text was cut for an ellipsis
        bool overflowed = false;  ///< a line is wider or the text is taller than the box (with Clip, the drawer clips)

        /// Every field back to its default; glyphs and lines are emptied with their capacity kept, so the next layout into this result does not allocate
        void Clear() {
            glyphs.clear();
            lines.clear();
            width = 0.0f;
            height = 0.0f;
            fontSize = 0.0f;
            lineHeight = 0.0f;
            truncated = false;
            overflowed = false;
        }
    };

    /**
     * @brief Breaks UTF-8 text into lines and places its glyphs. Pure code: it needs a Font, never a window or a GPU.
     *
     * Rules: '\n' ends a line, '\r' is ignored, '\t' is four spaces. With wrap, a line breaks at the last space that
     * fits; the spaces at the break are dropped (they hang off the end of the line and do not count in its width); a
     * word wider than the box breaks between characters. A code point the font lacks is laid out with its fallback glyph
     * (Font::ResolveGlyph). Alignment is inside maxWidth, or inside the widest line when there is no maxWidth.
     */
    class TextLayout {
    public:
        /// Lays @p utf8 out into a new result; a wrapper of the form below
        [[nodiscard]] static TextLayoutResult Layout(const Font& font, StringView utf8, const TextLayoutOptions& options);

        /// Lays @p utf8 out into @p into, which ends as the by-value form returns it (a font that is not loaded or a font size of 0 leaves it empty).
        /// The capacity of its glyphs and lines is kept and grown to the laid text, so a text that changes again and again allocates nothing once @p into has held a larger one
        static void Layout(const Font& font, StringView utf8, const TextLayoutOptions& options, TextLayoutResult& into);
    };

}  // namespace BECore
