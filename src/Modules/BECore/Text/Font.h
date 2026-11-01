#pragma once

#include <BECore/Resource/IResource.h>

namespace BECore {

    /// Vertical metrics of a font at one size, in pixels. Ascent is above the baseline (positive), descent below it (negative).
    struct FontVerticalMetrics {
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;

        /// Distance between two baselines
        [[nodiscard]] float LineHeight() const {
            return ascent - descent + lineGap;
        }
    };

    /// A glyph drawn as coverage (0 to 255) at one size. The offset is from the pen position on the baseline to the
    /// top-left corner of the bitmap, x to the right and y down, so a glyph that rises above the baseline has a negative y.
    struct GlyphBitmap {
        uint32_t width = 0;
        uint32_t height = 0;
        int32_t offsetX = 0;
        int32_t offsetY = 0;
        eastl::vector<uint8_t> coverage;
    };

    /**
     * @brief A TrueType or OpenType font file: the bytes, parsed on the CPU.
     *
     * A font needs no window and no GPU, so it loads on a pool thread (FontLoader) and measuring text with it is testable.
     * Turning glyphs into textures is FontAtlas's job and happens on the main thread.
     *
     * Sizes are CSS-like: a size of 16 maps the em square of the font to 16 pixels (not the ascent plus descent).
     * Glyph indices are the font's own; 0 is the font's .notdef glyph. All queries are const and thread safe.
     *
     * The parser does no range checking of offsets inside the file, so load fonts of the game only, never user input.
     */
    class Font : public IResource {
        BE_CLASS(Font)
    public:
        Font();
        ~Font() override;

        /// Parses font data from memory; @p name is what GetPath() answers. A font that cannot be parsed is in the Failed state.
        [[nodiscard]] static IntrusivePtr<Font> CreateFromMemory(PoolString name, eastl::vector<uint8_t> data);

        ResourceState GetState() const override;
        PoolString GetPath() const override;
        uint64_t GetMemoryUsage() const override;
        PoolString GetTypeName() const override;

        /// The glyph of @p codePoint, or 0 when the font has none
        [[nodiscard]] uint32_t FindGlyph(char32_t codePoint) const;

        /// The glyph to draw for @p codePoint: its own, else U+FFFD, else '?', else the .notdef glyph (0)
        [[nodiscard]] uint32_t ResolveGlyph(char32_t codePoint) const;

        [[nodiscard]] FontVerticalMetrics GetVerticalMetrics(float fontSize) const;
        [[nodiscard]] float GetAdvance(uint32_t glyph, float fontSize) const;

        /// The extra advance between two glyphs from the 'kern' table; fonts that keep kerning in GPOS only give 0
        [[nodiscard]] float GetKerning(uint32_t left, uint32_t right, float fontSize) const;

        /// Draws one glyph. An empty bitmap (width or height 0) is a glyph with no ink, such as a space.
        [[nodiscard]] GlyphBitmap RasterizeGlyph(uint32_t glyph, float fontSize) const;

    private:
        struct Impl;

        void SetFailed(PoolString name);

        eastl::vector<uint8_t> _data;
        eastl::unique_ptr<Impl> _impl;
        PoolString _path;
        ResourceState _state = ResourceState::Unloaded;
    };

}  // namespace BECore
