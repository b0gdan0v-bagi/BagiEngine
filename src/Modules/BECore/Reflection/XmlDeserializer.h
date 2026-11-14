#pragma once

#include <BECore/Reflection/IDeserializer.h>

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
         *
         * Refused with an error, before the file is read, when the path is not a regular file or the file is over
         * ReaderLimits::kMaxXmlDocumentBytes. Any load drops the earlier document: after a refusal nothing is readable.
         *
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
        bool LoadFromVirtualPath(StringView virtualPath);

        /**
         * @brief Load XML from string
         *
         * The text is UTF-8 whatever its first bytes are, and at most ReaderLimits::kMaxXmlDocumentBytes. Any load drops the earlier
         * document: after a refusal nothing is readable.
         *
         * @param xmlContent XML string content
         * @return true on success
         */
        bool LoadFromString(StringView xmlContent);

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

        bool Read(StringView name, bool& outValue) override;
        bool Read(StringView name, int8_t& outValue) override;
        bool Read(StringView name, uint8_t& outValue) override;
        bool Read(StringView name, int16_t& outValue) override;
        bool Read(StringView name, uint16_t& outValue) override;
        bool Read(StringView name, int32_t& outValue) override;
        bool Read(StringView name, uint32_t& outValue) override;
        bool Read(StringView name, int64_t& outValue) override;
        bool Read(StringView name, uint64_t& outValue) override;
        bool Read(StringView name, float& outValue) override;
        bool Read(StringView name, double& outValue) override;
        bool Read(StringView name, eastl::string& outValue) override;
        bool Read(StringView name, PoolString& outValue) override;

        // Attribute deserialization (from XML attributes)
        bool ReadAttribute(StringView name, bool& outValue) override;
        bool ReadAttribute(StringView name, int8_t& outValue) override;
        bool ReadAttribute(StringView name, uint8_t& outValue) override;
        bool ReadAttribute(StringView name, int16_t& outValue) override;
        bool ReadAttribute(StringView name, uint16_t& outValue) override;
        bool ReadAttribute(StringView name, int32_t& outValue) override;
        bool ReadAttribute(StringView name, uint32_t& outValue) override;
        bool ReadAttribute(StringView name, int64_t& outValue) override;
        bool ReadAttribute(StringView name, uint64_t& outValue) override;
        bool ReadAttribute(StringView name, float& outValue) override;
        bool ReadAttribute(StringView name, double& outValue) override;
        bool ReadAttribute(StringView name, eastl::string& outValue) override;
        bool ReadAttribute(StringView name, PoolString& outValue) override;

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

        // Strict values (see IDeserializer::SetStrictValues)
        void SetStrictValues(bool strict) override {
            _strictValues = strict;
        }
        bool IsStrictValues() const override {
            return _strictValues;
        }

        // Error tracking
        bool HasErrors() const override;
        const eastl::vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        /**
         * @brief Drop the document and every node of it that a stack holds, before another one is loaded
         */
        void ResetDocument();

        /**
         * @brief Whether one more open object, array or element would pass ReaderLimits::kMaxXmlNesting; reports it under @p name when it would
         */
        bool IsNestedTooDeep(StringView name);

        /**
         * @brief Get child node for reading
         */
        pugi::xml_node GetChild(StringView name) const;

        /**
         * @brief Get attribute from current node for reading
         */
        pugi::xml_attribute GetAttribute(StringView name) const;

        /**
         * @brief Strict read of an attribute: the whole text must be a value of T
         *
         * A missing attribute is false with no error, as in the lenient read; a malformed
         * text is reported under the name and the target keeps its value.
         */
        template <typename T>
        bool ReadStrictAttribute(StringView name, T& outValue);

        /**
         * @brief Strict read of a child element (see ReadStrictAttribute)
         */
        template <typename T>
        bool ReadStrictElement(StringView name, T& outValue);

        template <typename T>
        bool ApplyStrictText(StringView name, StringView text, T& outValue);

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

        bool _strictValues = false;
    };

}  // namespace BECore
