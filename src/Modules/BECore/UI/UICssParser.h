#pragma once

#include <BECore/UI/UIVariant.h>
#include <BECore/UI/UIWebReport.h>

namespace BECore {

    /// How a compound selector relates to the one on its left
    CORE_ENUM(UICssCombinator, uint8_t, None, Descendant, Child)

    /// The pseudo-classes the front-end understands: the states of a button
    CORE_ENUM(UICssPseudo, uint8_t, None, Hover, Active, Disabled, Checked)

    struct UICssDeclaration {
        static constexpr uint32_t HtmlSource = 0xFFFFFFFFu;  ///< `sourceId` of a declaration from a `style=""` attribute

        String property;  ///< lower case
        String value;
        bool important = false;
        uint32_t line = 0;
        uint32_t sourceId = 0;  ///< index into UICssSheet::sources
    };

    /// `tag#id.class.class:hover`, with the combinator that joins it to the compound on its left
    struct UICssCompound {
        UICssCombinator combinator = UICssCombinator::None;
        String tag;  ///< lower case; empty matches any element
        String id;
        Vector<String> classes;
        UICssPseudo pseudo = UICssPseudo::None;  ///< only on the last compound of a selector
    };

    struct UICssSelector {
        Vector<UICssCompound> compounds;  ///< left to right; the last one is the element itself
        uint32_t specificity = 0;         ///< ids << 16 | classes and pseudo-classes << 8 | tags
    };

    struct UICssRule {
        Vector<UICssSelector> selectors;  ///< a rule with a selector list is one rule
        Vector<UICssDeclaration> declarations;
        int media = -1;  ///< index into UICssSheet::medias, -1 outside every `@media`
        uint32_t order = 0;
    };

    /// An `@media` block: the block applies while any of the alternatives holds (a comma in the query)
    struct UICssMedia {
        Vector<UIVariantCondition> alternatives;
        uint32_t line = 0;
        uint32_t sourceId = 0;
    };

    struct UICssSheet {
        Vector<String> sources;  ///< "css" or "css:<href>", for the issues
        Vector<UICssRule> rules;
        Vector<UICssMedia> medias;
    };

    /**
     * @brief A reader for the CSS subset the web-style UI front-end uses.
     *
     * Reads rules with selector lists, comments, `!important`, and `@media` blocks whose queries are `and`-joined `(feature: value)` terms
     * (`orientation`, `min-` / `max-` `width`, `height` and `aspect-ratio`) separated by commas. A query becomes a UIVariantCondition, so a media block is the
     * same data as a `<variant when="...">` of the XML front-end. Selectors are a tag, `#id`, `.class`, a compound of them, `:hover` / `:active` /
     * `:disabled` / `:checked` on the last one, and the descendant (space) and child (`>`) combinators. Everything else (attribute selectors, `+`, `~`, other pseudo-classes
     * and pseudo-elements, `@import`, `@font-face`, `@keyframes`, nested `@media`) is reported as an issue and left out. A declaration is kept as text; the
     * compiler decides whether it can apply it.
     *
     * **Limits** (the same reason as the ones of `UIHtmlParser`: a style sheet is content that can be made as large as one likes). A sheet over its size, rules or
     * declarations over theirs are one entry in the report and the rest of the sheet is left out (what was read stays); a block, a selector or a media query over its limit is
     * left out with an entry, and the sheet goes on after it.
     */
    class UICssParser {
    public:
        /// Bytes of one style sheet read; the rest of a longer one is left out
        static constexpr size_t kMaxSheetBytes = 1024 * 1024;
        /// Rules of one sheet (an `@media` block counts its own rules too), and declarations of all rules of one sheet
        static constexpr size_t kMaxRules = 1024;
        static constexpr size_t kMaxDeclarations = 16 * 1024;
        static constexpr size_t kMaxDeclarationsPerRule = 256;
        static constexpr size_t kMaxMediaBlocks = 16;
        static constexpr size_t kMaxMediaAlternatives = 8;
        /// Selectors of a rule's list, parts of a selector (`a > b c` has three) and bytes of one selector
        static constexpr size_t kMaxSelectorsPerRule = 32;
        static constexpr size_t kMaxSelectorParts = 16;
        static constexpr size_t kMaxSelectorBytes = 1024;

        /// Reads @p css and adds its rules to @p sheet after the ones already there. @p sourceName names the text in the issues, @p firstLine is the line of its first character in the file
        static void ParseInto(UICssSheet& sheet, StringView css, StringView sourceName, uint32_t firstLine, UIWebReport& report);

        /// The declarations of a `style=""` attribute
        [[nodiscard]] static Vector<UICssDeclaration> ParseDeclarations(StringView text, uint32_t line, uint32_t sourceId, UIWebReport& report);
    };

}  // namespace BECore
