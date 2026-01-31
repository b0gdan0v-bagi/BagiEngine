#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <BECore/PoolString/PoolString.h>
#include <cstdint>

namespace BECore {

    /**
     * @brief Base interface for archive operations (shared between serializer and deserializer)
     *
     * Provides common methods for object/array nesting that are used by both
     * serialization (writing) and deserialization (reading).
     */
    class IArchiveBase {
    public:
        virtual ~IArchiveBase() = default;

        // =================================================================
        // Object nesting
        // =================================================================

        /**
         * @brief Begin a nested object/section
         *
         * When writing: creates a new named section
         * When reading: navigates into the named section
         *
         * @param name Name of the nested object
         * @return true if object exists (for reading) or was created (for writing)
         */
        virtual bool BeginObject(eastl::string_view name) = 0;

        /**
         * @brief End the current nested object/section
         */
        virtual void EndObject() = 0;

        // =================================================================
        // Array support
        // =================================================================

        /**
         * @brief Begin an array section
         *
         * When writing: creates a new named array with specified element name
         * When reading: navigates into the array, count receives the element count
         *
         * @param name Name of the array container
         * @param elementName Name for individual array elements (e.g., "item")
         * @param count [in/out] Number of elements
         * @return true if array exists or was created
         */
        virtual bool BeginArray(eastl::string_view name, eastl::string_view elementName, size_t& count) = 0;

        /**
         * @brief End the current array section
         */
        virtual void EndArray() = 0;

    protected:
        IArchiveBase() = default;
        IArchiveBase(const IArchiveBase&) = default;
        IArchiveBase& operator=(const IArchiveBase&) = default;
        IArchiveBase(IArchiveBase&&) = default;
        IArchiveBase& operator=(IArchiveBase&&) = default;
    };

    /**
     * @brief Interface for serialization (writing data)
     *
     * Provides methods for writing primitive types and strings to various
     * storage formats (XML, binary, etc.).
     *
     * @example
     * XmlSerializer serializer;
     * serializer.BeginObject("player");
     * serializer.WriteAttribute("health", 100);
     * serializer.WriteAttribute("name", "Hero");
     * serializer.EndObject();
     * serializer.SaveToFile("save.xml");
     */
    class ISerializer : public IArchiveBase {
    public:
        virtual ~ISerializer() = default;

        // =================================================================
        // Primitive types serialization (as child elements)
        // =================================================================

        virtual void Write(eastl::string_view name, bool value) = 0;
        virtual void Write(eastl::string_view name, int8_t value) = 0;
        virtual void Write(eastl::string_view name, uint8_t value) = 0;
        virtual void Write(eastl::string_view name, int16_t value) = 0;
        virtual void Write(eastl::string_view name, uint16_t value) = 0;
        virtual void Write(eastl::string_view name, int32_t value) = 0;
        virtual void Write(eastl::string_view name, uint32_t value) = 0;
        virtual void Write(eastl::string_view name, int64_t value) = 0;
        virtual void Write(eastl::string_view name, uint64_t value) = 0;
        virtual void Write(eastl::string_view name, float value) = 0;
        virtual void Write(eastl::string_view name, double value) = 0;

        // =================================================================
        // String types serialization (as child elements)
        // =================================================================

        virtual void Write(eastl::string_view name, eastl::string_view value) = 0;
        virtual void Write(eastl::string_view name, const PoolString& value) = 0;

        // =================================================================
        // Attribute serialization (for XML attributes, inline for binary)
        // =================================================================

        virtual void WriteAttribute(eastl::string_view name, bool value) = 0;
        virtual void WriteAttribute(eastl::string_view name, int8_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, uint8_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, int16_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, uint16_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, int32_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, uint32_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, int64_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, uint64_t value) = 0;
        virtual void WriteAttribute(eastl::string_view name, float value) = 0;
        virtual void WriteAttribute(eastl::string_view name, double value) = 0;
        virtual void WriteAttribute(eastl::string_view name, eastl::string_view value) = 0;
        virtual void WriteAttribute(eastl::string_view name, const PoolString& value) = 0;

    protected:
        ISerializer() = default;
        ISerializer(const ISerializer&) = default;
        ISerializer& operator=(const ISerializer&) = default;
        ISerializer(ISerializer&&) = default;
        ISerializer& operator=(ISerializer&&) = default;
    };

}  // namespace BECore
