#pragma once

#include <BECore/Reflection/IDeserializer.h>
#include <pugixml.hpp>
#include <EASTL/vector.h>
#include <filesystem>

namespace BECore {

    // Forward declarations
    class XmlConfig;
    class XmlNode;

    /**
     * @brief XML-based deserializer (reader) using pugixml
     *
     * Provides XML deserialization from save files and configuration,
     * with error tracking for missing or malformed data.
     *
     * @example
     * XmlDeserializer deserializer;
     * deserializer.LoadFromFile("save.xml");
     * deserializer.BeginObject("player");
     * int32_t health;
     * deserializer.ReadAttribute("health", health);
     * deserializer.EndObject();
     * 
     * if (deserializer.HasErrors()) {
     *     for (const auto& err : deserializer.GetErrors()) {
     *         LOG_ERROR("Error: {}", err.errorMessage);
     *     }
     * }
     */
    class XmlDeserializer : public IDeserializer {
    public:
        /**
         * @brief Construct an XML deserializer
         */
        XmlDeserializer();
        ~XmlDeserializer() override = default;

        // Non-copyable, movable
        XmlDeserializer(const XmlDeserializer&) = delete;
        XmlDeserializer& operator=(const XmlDeserializer&) = delete;
        XmlDeserializer(XmlDeserializer&&) = default;
        XmlDeserializer& operator=(XmlDeserializer&&) = default;

        // =================================================================
        // File I/O
        // =================================================================

        /**
         * @brief Load XML from file
         * @param path File path to load from
         * @return true on success
         */
        bool LoadFromFile(const std::filesystem::path& path);

        /**
         * @brief Load XML from virtual path
         * 
         * Resolves path via CoreManager::GetFileSystem() and loads the file.
         * 
         * @param virtualPath Virtual path (e.g., "config/game.xml")
         * @return true on success
         */
        bool LoadFromVirtualPath(eastl::string_view virtualPath);

        /**
         * @brief Load XML from string
         * @param xmlContent XML string content
         * @return true on success
         */
        bool LoadFromString(eastl::string_view xmlContent);

        /**
         * @brief Position deserializer at the given XmlNode
         * 
         * Allows using XmlDeserializer with nodes obtained from XmlConfig/ConfigManager.
         * 
         * @param node XmlNode to position at
         * @return true on success
         */
        bool LoadFromXmlNode(const XmlNode& node);

        // =================================================================
        // IDeserializer interface - Read methods
        // =================================================================

        bool Read(eastl::string_view name, bool& outValue) override;
        bool Read(eastl::string_view name, int8_t& outValue) override;
        bool Read(eastl::string_view name, uint8_t& outValue) override;
        bool Read(eastl::string_view name, int16_t& outValue) override;
        bool Read(eastl::string_view name, uint16_t& outValue) override;
        bool Read(eastl::string_view name, int32_t& outValue) override;
        bool Read(eastl::string_view name, uint32_t& outValue) override;
        bool Read(eastl::string_view name, int64_t& outValue) override;
        bool Read(eastl::string_view name, uint64_t& outValue) override;
        bool Read(eastl::string_view name, float& outValue) override;
        bool Read(eastl::string_view name, double& outValue) override;
        bool Read(eastl::string_view name, eastl::string& outValue) override;
        bool Read(eastl::string_view name, PoolString& outValue) override;

        // Attribute deserialization (from XML attributes)
        bool ReadAttribute(eastl::string_view name, bool& outValue) override;
        bool ReadAttribute(eastl::string_view name, int8_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, uint8_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, int16_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, uint16_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, int32_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, uint32_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, int64_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, uint64_t& outValue) override;
        bool ReadAttribute(eastl::string_view name, float& outValue) override;
        bool ReadAttribute(eastl::string_view name, double& outValue) override;
        bool ReadAttribute(eastl::string_view name, eastl::string& outValue) override;
        bool ReadAttribute(eastl::string_view name, PoolString& outValue) override;

        bool BeginObject(eastl::string_view name) override;
        void EndObject() override;

        bool BeginArray(eastl::string_view name, eastl::string_view elementName, size_t& count) override;
        void EndArray() override;

        // Error tracking
        bool HasErrors() const override;
        const eastl::vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    private:
        /**
         * @brief Get child node for reading
         */
        pugi::xml_node GetChild(eastl::string_view name) const;

        /**
         * @brief Get attribute from current node for reading
         */
        pugi::xml_attribute GetAttribute(eastl::string_view name) const;

        /**
         * @brief Add error to the error list
         */
        void AddError(eastl::string_view fieldName, eastl::string_view errorMessage);

        pugi::xml_document _document;
        eastl::vector<pugi::xml_node> _nodeStack;
        
        // For array iteration
        struct ArrayContext {
            pugi::xml_node parentNode;
            eastl::vector<pugi::xml_node> elements;
            size_t currentIndex = 0;
        };
        eastl::vector<ArrayContext> _arrayStack;

        // Error tracking
        eastl::vector<DeserializeError> _errors;
    };

}  // namespace BECore
