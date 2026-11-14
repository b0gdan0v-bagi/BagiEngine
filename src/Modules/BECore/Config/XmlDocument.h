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

        /// Creates the missing parent folders; writes two-space indentation, one attribute per line
        bool SaveToFile(const std::filesystem::path& filepath) const;

        /// The first child of the document (its root element), or an empty node
        XmlNode GetRoot() const;

        void Clear();

        bool IsLoaded() const;

    private:
        pugi::xml_document _doc;
    };

}  // namespace BECore
