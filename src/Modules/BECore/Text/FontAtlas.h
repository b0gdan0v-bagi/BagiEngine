#pragma once

#include <BECore/Text/Font.h>
#include <BECore/Text/TextSettings.h>

namespace BECore {

    class IRenderer;
    class ITexture;

    /// Where the picture of one glyph is, and where it sits relative to the pen
    struct AtlasGlyph {
        int32_t page = -1;     ///< index of the page texture; -1 for a glyph with no ink (a space)
        Rect source;           ///< the glyph's texels in the page
        float offsetX = 0.0f;  ///< from the pen position on the baseline to the left edge of the picture
        float offsetY = 0.0f;  ///< to the top edge of the picture (negative above the baseline)
    };

    /**
     * @brief The glyphs of one font at one pixel size, drawn into page textures.
     *
     * A page is made whole by IRenderer::CreateTexture() and never changed or freed while the atlas lives, so a frame in
     * flight can never be reading a page that is being written. Glyphs that are missing are added in new pages
     * (Prepare()), not in the old ones. Glyphs are white with the coverage in alpha: the color comes from the tint of the draw.
     *
     * With `outlinePixels` above 0 the pictures are the glyphs grown by that many pixels (GlyphOutline::Dilate()), for the
     * outline of a text; their offsets follow the grown picture, and the page padding stays 1 px.
     *
     * Not thread safe and main thread only: it makes GPU objects.
     */
    class FontAtlas : public RefCounted {
    public:
        /// A transparent border around every glyph, so that linear filtering never picks up its neighbour
        static constexpr uint32_t kPadding = 1;

        FontAtlas(IntrusivePtr<Font> font, int pixelSize, uint32_t pageSize, int outlinePixels = 0);

        [[nodiscard]] const Font& GetFont() const {
            return *_font;
        }
        [[nodiscard]] int GetPixelSize() const {
            return _pixelSize;
        }
        /// 0 for the plain glyphs, else how many pixels the pictures are grown by on every side
        [[nodiscard]] int GetOutlinePixels() const {
            return _outlinePixels;
        }

        /**
         * @brief Draws the glyphs that the atlas does not have yet and uploads them as new pages.
         * @return false when a page could not be made; its glyphs are then left without a picture (and not retried)
         */
        bool Prepare(IRenderer& renderer, Span<const uint32_t> glyphs);

        /// Prepare() for the glyphs that the font has for the code points of the ranges (a code point it lacks is skipped, not replaced)
        bool PrepareCodePoints(IRenderer& renderer, Span<const GlyphRange> ranges);

        /// Null until Prepare() has seen the glyph
        [[nodiscard]] const AtlasGlyph* Find(uint32_t glyph) const;
        [[nodiscard]] ITexture* GetPage(int32_t page) const;

        [[nodiscard]] size_t GetPageCount() const {
            return _pages.size();
        }
        [[nodiscard]] size_t GetGlyphCount() const {
            return _glyphs.size();
        }

    private:
        struct Pending {
            uint32_t glyph = 0;
            GlyphBitmap bitmap;
        };
        struct Placed {
            size_t pending = 0;
            uint32_t x = 0;
            uint32_t y = 0;
        };

        void BuildPages(IRenderer& renderer, Vector<Pending>& pending, bool& ok);

        IntrusivePtr<Font> _font;
        int _pixelSize = 0;
        int _outlinePixels = 0;
        uint32_t _pageSize = 0;
        HashMap<uint32_t, AtlasGlyph> _glyphs;
        Vector<IntrusivePtr<ITexture>> _pages;
    };

}  // namespace BECore
