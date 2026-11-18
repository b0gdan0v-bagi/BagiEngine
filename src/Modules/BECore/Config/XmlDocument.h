#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Types/Types.h>
#include <filesystem>
#include <pugixml.hpp>

namespace BECore {

    class XmlNode;

    /**
     * @brief Owns a pugi::xml_document; every XmlNode taken from it is valid only while the document lives
     */
    class XmlDocument : public RefCountedAtomic {
    public:
        XmlDocument() = default;
        ~XmlDocument() = default;

        /// @return false when the file is missing, is not a regular file or does not parse
        bool LoadFromFile(const std::filesystem::path& filepath);

        /// @return false for an empty string or one that does not parse
        bool LoadFromString(StringView xmlContent);

        /**
         * @brief Creates the missing parent folders and writes the document, laid out like the file that is there
         *
         * The text is the document's, two-space indentation and every attribute of an element on one line; when the file
         * already exists the lines the edit did not touch keep its layout (blank lines, indentation) through MergeXmlLayout,
         * and the merge is checked: a text that does not parse to the document is not written. A file that cannot carry the
         * layout (quote style, `<a/>` forms, wrapped attributes, a carriage return, one that does not parse) is replaced by the canonical
         * text, which a later save then keeps. The file is not touched when the text is the one it holds.
         *
         * The comments and the XML declaration the file had are kept (they are nodes of the document), a declaration it did not
         * have is not added, and a UTF-8 byte order mark the document started with is written again. What is on disk never
         * wins over the document: a change made to the file after the load is overwritten.
         * @return false when the file cannot be opened for writing
         */
        bool SaveToFile(const std::filesystem::path& filepath) const;

        /// The root element of the document (the comments and the declaration before it are not the root), or an empty node
        XmlNode GetRoot() const;

        void Clear();

        bool IsLoaded() const;

    private:
        /// The text SaveToFile writes
        String Serialize() const;

        pugi::xml_document _doc;
        bool _hasByteOrderMark = false;
    };

}  // namespace BECore
