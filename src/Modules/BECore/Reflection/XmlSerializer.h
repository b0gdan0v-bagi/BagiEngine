#pragma once

#include <BECore/File/Path.h>
#include <BECore/Reflection/ISerializer.h>
#include <pugixml.hpp>

namespace BECore {

    class XmlConfig;
    class XmlNode;

    /**
     * @brief XML serializer (writer) on pugixml: human-readable save files and configuration
     *
     * @example
     * XmlSerializer serializer;
     * serializer.BeginObject("player");
     * serializer.WriteAttribute("health", 100);
     * serializer.EndObject();
     * serializer.SaveToFile("save.xml");
     */
    class XmlSerializer : public ISerializer {
    public:
        XmlSerializer();
        ~XmlSerializer() override = default;

        XmlSerializer(const XmlSerializer&) = delete;
        XmlSerializer& operator=(const XmlSerializer&) = delete;
        XmlSerializer(XmlSerializer&&) = default;
        XmlSerializer& operator=(XmlSerializer&&) = default;

        /// Writes the document to a file; true on success.
        bool SaveToFile(const Path& path) const;

        String SaveToString() const;

        using ISerializer::Write;
        using ISerializer::WriteAttribute;

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
         * @brief The root XML node, for copying serialized data into another XML tree without an intermediate serializer
         */
        const pugi::xml_node& GetRootNode() const {
            return _nodeStack.front();
        }

    protected:
        /// True outside every object, array and array element, where a write is a field of the serialized object itself
        bool IsAtRoot() const {
            return _nodeStack.size() == 1;
        }

    private:
        pugi::xml_node GetOrCreateChild(StringView name);

        pugi::xml_attribute GetOrCreateAttribute(StringView name);

        pugi::xml_document _document;
        Vector<pugi::xml_node> _nodeStack;

        struct ArrayContext {
            pugi::xml_node parentNode;
            String elementName;
            size_t currentIndex = 0;
        };
        Vector<ArrayContext> _arrayStack;
    };

}  // namespace BECore
