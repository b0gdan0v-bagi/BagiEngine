#pragma once

#include <BECore/Reflection/ISerializer.h>

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

        void Write(StringView name, bool value) override;
        void Write(StringView name, int8_t value) override;
        void Write(StringView name, uint8_t value) override;
        void Write(StringView name, int16_t value) override;
        void Write(StringView name, uint16_t value) override;
        void Write(StringView name, int32_t value) override;
        void Write(StringView name, uint32_t value) override;
        void Write(StringView name, int64_t value) override;
        void Write(StringView name, uint64_t value) override;
        void Write(StringView name, float value) override;
        void Write(StringView name, double value) override;
        void Write(StringView name, StringView value) override;
        void Write(StringView name, const PoolString& value) override;

        // Attribute serialization (primitives stored as XML attributes)
        void WriteAttribute(StringView name, bool value) override;
        void WriteAttribute(StringView name, int8_t value) override;
        void WriteAttribute(StringView name, uint8_t value) override;
        void WriteAttribute(StringView name, int16_t value) override;
        void WriteAttribute(StringView name, uint16_t value) override;
        void WriteAttribute(StringView name, int32_t value) override;
        void WriteAttribute(StringView name, uint32_t value) override;
        void WriteAttribute(StringView name, int64_t value) override;
        void WriteAttribute(StringView name, uint64_t value) override;
        void WriteAttribute(StringView name, float value) override;
        void WriteAttribute(StringView name, double value) override;
        void WriteAttribute(StringView name, StringView value) override;
        void WriteAttribute(StringView name, const PoolString& value) override;

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

        /**
         * @brief Get the root XML node for direct manipulation
         *
         * Used to copy serialized component data into a different XML tree
         * without creating intermediate serializers.
         */
        const pugi::xml_node& GetRootNode() const {
            return _nodeStack.front();
        }

    private:
        /**
         * @brief Get or create child node with given name
         */
        pugi::xml_node GetOrCreateChild(StringView name);

        /**
         * @brief Get or create attribute on current node
         */
        pugi::xml_attribute GetOrCreateAttribute(StringView name);

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
