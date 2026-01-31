#pragma once

#include <BECore/Reflection/ISerializer.h>
#include <pugixml.hpp>
#include <EASTL/vector.h>
#include <filesystem>

namespace BECore {

    // Forward declarations
    class XmlConfig;
    class XmlNode;

    /**
     * @brief XML-based serializer (writer) using pugixml
     *
     * Provides human-readable XML serialization for save files and configuration.
     *
     * @example
     * XmlSerializer serializer;
     * serializer.BeginObject("player");
     * serializer.WriteAttribute("health", 100);
     * serializer.WriteAttribute("name", "Hero");
     * serializer.EndObject();
     * serializer.SaveToFile("save.xml");
     */
    class XmlSerializer : public ISerializer {
    public:
        /**
         * @brief Construct an XML serializer
         */
        XmlSerializer();
        ~XmlSerializer() override = default;

        // Non-copyable, movable
        XmlSerializer(const XmlSerializer&) = delete;
        XmlSerializer& operator=(const XmlSerializer&) = delete;
        XmlSerializer(XmlSerializer&&) = default;
        XmlSerializer& operator=(XmlSerializer&&) = default;

        // =================================================================
        // File I/O
        // =================================================================

        /**
         * @brief Save XML to file
         * @param path File path to save to
         * @return true on success
         */
        bool SaveToFile(const std::filesystem::path& path) const;

        /**
         * @brief Get XML as string
         * @return XML content as string
         */
        eastl::string SaveToString() const;

        // =================================================================
        // ISerializer interface - Write methods
        // =================================================================

        void Write(eastl::string_view name, bool value) override;
        void Write(eastl::string_view name, int8_t value) override;
        void Write(eastl::string_view name, uint8_t value) override;
        void Write(eastl::string_view name, int16_t value) override;
        void Write(eastl::string_view name, uint16_t value) override;
        void Write(eastl::string_view name, int32_t value) override;
        void Write(eastl::string_view name, uint32_t value) override;
        void Write(eastl::string_view name, int64_t value) override;
        void Write(eastl::string_view name, uint64_t value) override;
        void Write(eastl::string_view name, float value) override;
        void Write(eastl::string_view name, double value) override;
        void Write(eastl::string_view name, eastl::string_view value) override;
        void Write(eastl::string_view name, const PoolString& value) override;

        // Attribute serialization (primitives stored as XML attributes)
        void WriteAttribute(eastl::string_view name, bool value) override;
        void WriteAttribute(eastl::string_view name, int8_t value) override;
        void WriteAttribute(eastl::string_view name, uint8_t value) override;
        void WriteAttribute(eastl::string_view name, int16_t value) override;
        void WriteAttribute(eastl::string_view name, uint16_t value) override;
        void WriteAttribute(eastl::string_view name, int32_t value) override;
        void WriteAttribute(eastl::string_view name, uint32_t value) override;
        void WriteAttribute(eastl::string_view name, int64_t value) override;
        void WriteAttribute(eastl::string_view name, uint64_t value) override;
        void WriteAttribute(eastl::string_view name, float value) override;
        void WriteAttribute(eastl::string_view name, double value) override;
        void WriteAttribute(eastl::string_view name, eastl::string_view value) override;
        void WriteAttribute(eastl::string_view name, const PoolString& value) override;

        bool BeginObject(eastl::string_view name) override;
        void EndObject() override;

        bool BeginArray(eastl::string_view name, eastl::string_view elementName, size_t& count) override;
        void EndArray() override;

    private:
        /**
         * @brief Get or create child node with given name
         */
        pugi::xml_node GetOrCreateChild(eastl::string_view name);

        /**
         * @brief Get or create attribute on current node
         */
        pugi::xml_attribute GetOrCreateAttribute(eastl::string_view name);

        pugi::xml_document _document;
        eastl::vector<pugi::xml_node> _nodeStack;
        
        // For array iteration
        struct ArrayContext {
            pugi::xml_node parentNode;
            eastl::string elementName;
            size_t currentIndex = 0;
        };
        eastl::vector<ArrayContext> _arrayStack;
    };

}  // namespace BECore
