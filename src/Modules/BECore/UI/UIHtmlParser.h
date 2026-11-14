#pragma once

#include <BECore/UI/UIWebReport.h>

namespace BECore {

    struct UIHtmlAttribute {
        String name;  ///< lower case
        String value;
    };

    /// A node of a parsed HTML document: an element (`tag` is set) or a piece of text. Nodes refer to each other by index into UIHtmlDocument::nodes
    struct UIHtmlNode {
        static constexpr int NoNode = -1;

        String tag;   ///< lower case; empty for a text node and for the document node
        String text;  ///< a text node: its characters with the entities decoded (the text of `<style>` is raw)
        Vector<UIHtmlAttribute> attributes;
        int parent = NoNode;
        Vector<int> children;
        uint32_t line = 0;

        [[nodiscard]] bool IsElement() const {
            return !tag.empty();
        }

        /// The value of the attribute, null when there is none
        [[nodiscard]] const String* FindAttribute(StringView name) const;
        [[nodiscard]] bool HasAttribute(StringView name) const {
            return FindAttribute(name) != nullptr;
        }
        [[nodiscard]] bool HasClass(StringView className) const;
    };

    /// A parsed document: `nodes[0]` is the document node, a parent always comes before its children
    struct UIHtmlDocument {
        static constexpr int RootIndex = 0;
        Vector<UIHtmlNode> nodes;
    };

    /**
     * @brief A small HTML reader for the web-style UI front-end, not a browser's parser.
     *
     * Reads tags, attributes (quoted or not), text, comments, a doctype, the void elements (`img`, `br`, `hr`, `input`, `link`, `meta`) and the raw text of
     * `<style>` and `<script>`. The entities `&amp; &lt; &gt; &quot; &apos; &nbsp;` and numeric ones are decoded. It does not repair markup the way a browser
     * does: an end tag closes the nearest open element of that name, a stray one is reported, and `<p>` and `<li>` are not closed by the next sibling, so every
     * element needs its end tag.
     *
     * **Limits.** A file is content a modder or a damaged download can make as large and as deep as it likes, so the parser keeps to these (about a hundred times
     * what the largest screen of the engine and its games needs, where a cost is a product with the style sheet). A document, a nesting or a node count over its
     * limit is one entry in the report and the rest of the document is left out (what was read stays); an element with more attributes, or an attribute longer, than
     * the limit loses those attributes with an entry.
     */
    class UIHtmlParser {
    public:
        /// Bytes of the document read; the rest of a longer one is left out
        static constexpr size_t kMaxDocumentBytes = 1024 * 1024;
        /// Elements and pieces of text (the document node not counted)
        static constexpr size_t kMaxNodes = 8192;
        /// Open elements inside each other. The compile walks the tree recursively, and the selector matching of the compiler counts levels in a 64-bit mask
        static constexpr size_t kMaxDepth = 64;
        static constexpr size_t kMaxAttributes = 64;
        /// Bytes of the name and of the value of an attribute
        static constexpr size_t kMaxAttributeBytes = 16 * 1024;

        [[nodiscard]] static UIHtmlDocument Parse(StringView html, UIWebReport& report);
    };

}  // namespace BECore
