#pragma once

#include <BECore/Reflection/IArchive.h>
#include <pugixml.hpp>
#include <EASTL/vector.h>
#include <filesystem>

namespace BECore {

    /**
     * @brief XML-based archive implementation using pugixml
     *
     * Provides human-readable XML serialization for save files and configuration.
     *
     * @example
     * // Writing
     * XmlArchive archive(XmlArchive::Mode::Write);
     * Serialize(archive, "player", player);
     * archive.SaveToFile("save.xml");
     *
     * // Reading
     * XmlArchive archive(XmlArchive::Mode::Read);
     * archive.LoadFromFile("save.xml");
     * Serialize(archive, "player", player);
     */
    class XmlArchive : public IArchive {
    public:
        enum class Mode {
            Read,
            Write
        };

        /**
         * @brief Construct an XML archive
         * @param mode Read or Write mode
         */
        explicit XmlArchive(Mode mode);
        ~XmlArchive() override = default;

        // Non-copyable, movable
        XmlArchive(const XmlArchive&) = delete;
        XmlArchive& operator=(const XmlArchive&) = delete;
        XmlArchive(XmlArchive&&) = default;
        XmlArchive& operator=(XmlArchive&&) = default;

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
         * @brief Load XML from string
         * @param xmlContent XML string content
         * @return true on success
         */
        bool LoadFromString(eastl::string_view xmlContent);

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
        // IArchive interface
        // =================================================================

        bool IsReading() const override { return _mode == Mode::Read; }
        bool IsWriting() const override { return _mode == Mode::Write; }

        void Serialize(eastl::string_view name, bool& value) override;
        void Serialize(eastl::string_view name, int8_t& value) override;
        void Serialize(eastl::string_view name, uint8_t& value) override;
        void Serialize(eastl::string_view name, int16_t& value) override;
        void Serialize(eastl::string_view name, uint16_t& value) override;
        void Serialize(eastl::string_view name, int32_t& value) override;
        void Serialize(eastl::string_view name, uint32_t& value) override;
        void Serialize(eastl::string_view name, int64_t& value) override;
        void Serialize(eastl::string_view name, uint64_t& value) override;
        void Serialize(eastl::string_view name, float& value) override;
        void Serialize(eastl::string_view name, double& value) override;
        void Serialize(eastl::string_view name, eastl::string& value) override;
        void Serialize(eastl::string_view name, PoolString& value) override;

        bool BeginObject(eastl::string_view name) override;
        void EndObject() override;

        bool BeginArray(eastl::string_view name, size_t& count) override;
        void EndArray() override;

    private:
        /**
         * @brief Get or create child node with given name
         */
        pugi::xml_node GetOrCreateChild(eastl::string_view name);

        /**
         * @brief Get child node for reading
         */
        pugi::xml_node GetChild(eastl::string_view name) const;

        Mode _mode;
        pugi::xml_document _document;
        eastl::vector<pugi::xml_node> _nodeStack;
        
        // For array iteration
        struct ArrayContext {
            pugi::xml_node parentNode;
            eastl::vector<pugi::xml_node> elements;
            size_t currentIndex = 0;
        };
        eastl::vector<ArrayContext> _arrayStack;
    };

}  // namespace BECore
