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

        eastl::string property;  ///< lower case
        eastl::string value;
        bool important = false;
        uint32_t line = 0;
        uint32_t sourceId = 0;  ///< index into UICssSheet::sources
    };

    /// `tag#id.class.class:hover`, with the combinator that joins it to the compound on its left
    struct UICssCompound {
        UICssCombinator combinator = UICssCombinator::None;
        eastl::string tag;  ///< lower case; empty matches any element
        eastl::string id;
        eastl::vector<eastl::string> classes;
        UICssPseudo pseudo = UICssPseudo::None;  ///< only on the last compound of a selector
    };

    struct UICssSelector {
        eastl::vector<UICssCompound> compounds;  ///< left to right; the last one is the element itself
        uint32_t specificity = 0;                ///< ids << 16 | classes and pseudo-classes << 8 | tags
    };

    struct UICssRule {
        eastl::vector<UICssSelector> selectors;  ///< a rule with a selector list is one rule
        eastl::vector<UICssDeclaration> declarations;
        int media = -1;  ///< index into UICssSheet::medias, -1 outside every `@media`
        uint32_t order = 0;
    };

    /// An `@media` block: the block applies while any of the alternatives holds (a comma in the query)
    struct UICssMedia {
        eastl::vector<UIVariantCondition> alternatives;
        uint32_t line = 0;
        uint32_t sourceId = 0;
    };

    struct UICssSheet {
        eastl::vector<eastl::string> sources;  ///< "css" or "css:<href>", for the issues
        eastl::vector<UICssRule> rules;
        eastl::vector<UICssMedia> medias;
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
     */
    class UICssParser {
    public:
        /// Reads @p css and adds its rules to @p sheet after the ones already there. @p sourceName names the text in the issues, @p firstLine is the line of its first character in the file
        static void ParseInto(UICssSheet& sheet, eastl::string_view css, eastl::string_view sourceName, uint32_t firstLine, UIWebReport& report);

        /// The declarations of a `style=""` attribute
        [[nodiscard]] static eastl::vector<UICssDeclaration> ParseDeclarations(eastl::string_view text, uint32_t line, uint32_t sourceId, UIWebReport& report);
    };

}  // namespace BECore
