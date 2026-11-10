#pragma once

namespace BECore {

    class XmlNode;

    /// One font file of a family and the CSS weight it is drawn at (400 regular, 700 bold)
    struct FontFace {
        int weight = 400;
        PoolString path;
    };

    struct FontFamily {
        PoolString name;
        eastl::vector<FontFace> faces;
    };

    /// An inclusive range of code points
    struct GlyphRange {
        char32_t first = 0;
        char32_t last = 0;
    };

    /**
     * @brief Settings of the text system, read from `config/TextConfig.xml`.
     *
     * ```xml
     * <TextConfig defaultFont="assets/fonts/PT_Sans-Web-Regular.ttf" defaultFamily="PT Sans" pageSize="1024">
     *   <families>
     *     <family name="PT Sans"><face weight="400" path="..."/><face weight="700" path="..."/></family>
     *   </families>
     *   <preload><range name="Basic Latin" from="32" to="126"/>...</preload>
     * </TextConfig>
     * ```
     * `preload` lists the code points whose glyphs a FontAtlas draws up front, when a font is first used at a size;
     * any other glyph is drawn the first time a text needs it, in a page of its own.
     *
     * `families` lets a label name a face by family and weight instead of a file path (`ResolveFontPath`):
     * - the weight is clamped to 100..900; an empty family means `defaultFamily`;
     * - the face is the one of that family whose weight is nearest by the CSS rule: for a weight under 400 the
     *   faces below are looked at first (the nearest), then those above; for 400..500 the faces from the weight up to
     *   500, then those below, then those above 500; for a weight over 500 the faces above first, then those below;
     * - a family that is not in the table gives `defaultFont` and reports it through `known`; an empty family whose
     *   `defaultFamily` is not in the table also gives `defaultFont`, silently (a table without families is valid);
     * - a face with an empty path or a weight outside 1..1000, and a family with no valid face, are skipped with a
     *   warning; of two families with one name the first wins.
     */
    struct TextSettings {
        PoolString defaultFont = "assets/fonts/PT_Sans-Web-Regular.ttf"_intern;
        PoolString defaultFamily = "PT Sans"_intern;
        uint32_t pageSize = 1024;
        eastl::vector<GlyphRange> preload;
        eastl::vector<FontFamily> families;

        /// The values of the shipped TextConfig.xml: Basic Latin, Latin-1, Cyrillic and the general punctuation
        [[nodiscard]] static TextSettings Default();

        /// The file for @p family at @p weight (rules in the class comment). @p known, when given, is set to false only
        /// when a non-empty @p family is not in the table, so a caller can log it once
        [[nodiscard]] PoolString ResolveFontPath(PoolString family, int weight, bool* known = nullptr) const;

        /// Reads @p node over the defaults: a missing attribute keeps its default, a `preload` element replaces the ranges
        [[nodiscard]] static TextSettings FromConfig(const XmlNode& node);
    };

}  // namespace BECore
