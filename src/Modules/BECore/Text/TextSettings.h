#pragma once

namespace BECore {

    class XmlNode;

    /// An inclusive range of code points
    struct GlyphRange {
        char32_t first = 0;
        char32_t last = 0;
    };

    /**
     * @brief Settings of the text system, read from `config/TextConfig.xml`.
     *
     * ```xml
     * <TextConfig defaultFont="assets/fonts/PT_Sans-Web-Regular.ttf" pageSize="1024">
     *   <preload><range name="Basic Latin" from="32" to="126"/>...</preload>
     * </TextConfig>
     * ```
     * `preload` lists the code points whose glyphs a FontAtlas draws up front, when a font is first used at a size;
     * any other glyph is drawn the first time a text needs it, in a page of its own.
     */
    struct TextSettings {
        PoolString defaultFont = "assets/fonts/PT_Sans-Web-Regular.ttf"_intern;
        uint32_t pageSize = 1024;
        eastl::vector<GlyphRange> preload;

        /// The values of the shipped TextConfig.xml: Basic Latin, Latin-1, Cyrillic and the general punctuation
        [[nodiscard]] static TextSettings Default();

        /// Reads @p node over the defaults: a missing attribute keeps its default, a `preload` element replaces the ranges
        [[nodiscard]] static TextSettings FromConfig(const XmlNode& node);
    };

}  // namespace BECore
