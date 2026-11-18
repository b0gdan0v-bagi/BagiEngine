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
         * @brief Creates the missing parent folders and writes the document: two-space indentation, every attribute of an
         * element on one line
         *
         * The comments and the XML declaration the file had are kept (they are nodes of the document), a declaration it did not
         * have is not added, and a UTF-8 byte order mark it started with is written again.
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
