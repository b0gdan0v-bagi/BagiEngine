#pragma once

#include <BECore/UI/UIWebReport.h>

namespace BECore {

    struct UIHtmlAttribute {
        eastl::string name;  ///< lower case
        eastl::string value;
    };

    /// A node of a parsed HTML document: an element (`tag` is set) or a piece of text. Nodes refer to each other by index into UIHtmlDocument::nodes
    struct UIHtmlNode {
        static constexpr int NoNode = -1;

        eastl::string tag;   ///< lower case; empty for a text node and for the document node
        eastl::string text;  ///< a text node: its characters with the entities decoded (the text of `<style>` is raw)
        eastl::vector<UIHtmlAttribute> attributes;
        int parent = NoNode;
        eastl::vector<int> children;
        uint32_t line = 0;

        [[nodiscard]] bool IsElement() const {
            return !tag.empty();
        }

        /// The value of the attribute, null when there is none
        [[nodiscard]] const eastl::string* FindAttribute(eastl::string_view name) const;
        [[nodiscard]] bool HasAttribute(eastl::string_view name) const {
            return FindAttribute(name) != nullptr;
        }
        [[nodiscard]] bool HasClass(eastl::string_view className) const;
    };

    /// A parsed document: `nodes[0]` is the document node, a parent always comes before its children
    struct UIHtmlDocument {
        static constexpr int RootIndex = 0;
        eastl::vector<UIHtmlNode> nodes;
    };

    /**
     * @brief A small HTML reader for the web-style UI front-end, not a browser's parser.
     *
     * Reads tags, attributes (quoted or not), text, comments, a doctype, the void elements (`img`, `br`, `hr`, `input`, `link`, `meta`) and the raw text of
     * `<style>` and `<script>`. The entities `&amp; &lt; &gt; &quot; &apos; &nbsp;` and numeric ones are decoded. It does not repair markup the way a browser
     * does: an end tag closes the nearest open element of that name, a stray one is reported, and `<p>` and `<li>` are not closed by the next sibling, so every
     * element needs its end tag.
     */
    class UIHtmlParser {
    public:
        [[nodiscard]] static UIHtmlDocument Parse(eastl::string_view html, UIWebReport& report);
    };

}  // namespace BECore
