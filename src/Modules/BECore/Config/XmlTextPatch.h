#pragma once

#include <BECore/Types/Types.h>
#include <pugixml.hpp>

namespace BECore {

    /**
     * @brief Edits the attributes of start tags in the text of an XML file, byte for byte everywhere else
     *
     * A re-save through pugixml loses the line wrapping, the quote style and the `<a />` forms of a file a person wrote and
     * reads; this patches the text itself. Only the bytes of the named start tags can change: comments, the declaration, the
     * BOM, the text between elements and the end of the file stay as they are. Pure: no engine services, no file system.
     *
     * Parse the text with Parse, pick the elements from the document it fills, and give the same text to Apply: an element
     * is located by `xml_node::offset_debug()` and the start tag found there must still match the document (the name and the
     * attribute names, in order), otherwise nothing is written.
     */
    class XmlTextPatch {
    public:
        struct AttributeEdit {
            String name;
            /// A value sets the attribute, or adds it when the tag has none; empty removes it (no error when it is absent)
            Optional<String> value;
        };

        struct ElementEdit {
            pugi::xml_node element;
            Vector<AttributeEdit> attributes;
        };

        XmlTextPatch() = delete;

        /**
         * @brief Parses UTF-8 @p text keeping comments, the declaration and processing instructions
         *
         * The offsets of the nodes in @p doc are offsets into @p text. Refused: a UTF-16 or UTF-32 text (a byte order mark),
         * a NUL byte, and a text that does not parse; @p error says why.
         */
        static bool Parse(StringView text, pugi::xml_document& doc, String& error);

        /**
         * @brief Writes @p text with the attribute edits of @p edits into @p out
         *
         * A set keeps the attribute's place and quote character; an add goes after the last attribute (after the name when
         * there is none), before any `/>`, as a space and `name="value"` or, when the last attribute starts a line, as a line
         * with the same line break and indentation; a remove takes the attribute and the whitespace before it. Several
         * elements may be edited at once. An element twice, a node that is not an element, a start tag that does not match the
         * document, a bad attribute name or a control character in a value is an error: @p out is left as it was and @p error
         * says why. No edit returns the same bytes.
         */
        static bool Apply(StringView text, Span<const ElementEdit> edits, String& out, String& error);
    };

}  // namespace BECore
