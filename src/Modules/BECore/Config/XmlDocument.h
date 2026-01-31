#pragma once

#include <BECore/RefCounted/RefCounted.h>
#include <pugixml.hpp>
#include <filesystem>

namespace BECore {

    class XmlNode;

    /**
     * @brief XML document wrapper with ownership
     * 
     * Owns the underlying pugi::xml_document and provides lifetime management.
     * Use with IntrusivePtr for reference counting.
     */
    class XmlDocument : public RefCountedAtomic {
    public:
        XmlDocument() = default;
        ~XmlDocument() = default;

        /**
         * @brief Load XML from file
         * @param filepath Path to XML file
         * @return True if successfully loaded
         */
        bool LoadFromFile(const std::filesystem::path& filepath);

        /**
         * @brief Load XML from string
         * @param xmlContent XML content as string
         * @return True if successfully loaded
         */
        bool LoadFromString(eastl::string_view xmlContent);

        /**
         * @brief Save XML to file
         * @param filepath Path to save to
         * @return True if successfully saved
         */
        bool SaveToFile(const std::filesystem::path& filepath) const;

        /**
         * @brief Get root node of the document
         * @return XmlNode wrapping first child
         */
        XmlNode GetRoot() const;

        /**
         * @brief Clear the document
         */
        void Clear();

        /**
         * @brief Check if document is loaded
         * @return True if document has content
         */
        bool IsLoaded() const;

    private:
        pugi::xml_document _doc;
    };

}  // namespace BECore
