#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief Object and array nesting shared by ISerializer and IDeserializer.
     */
    class IArchiveBase {
    public:
        virtual ~IArchiveBase() = default;

        /**
         * @brief Begin a nested object/section
         *
         * When writing: creates a new named section
         * When reading: navigates into the named section
         *
         * @param name Name of the nested object
         * @return true if object exists (for reading) or was created (for writing)
         */
        virtual bool BeginObject(StringView name) = 0;

        virtual void EndObject() = 0;

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
        virtual bool BeginArray(StringView name, StringView elementName, size_t& count) = 0;

        virtual void EndArray() = 0;

        /**
         * @brief Begin the next element inside a BeginArray/EndArray scope
         *
         * When writing: creates a new child element with the array's element name.
         * When reading: navigates into the next child element in order.
         *
         * @return true if an element is available (reading) or was created (writing)
         */
        virtual bool BeginArrayElement() = 0;

        virtual void EndArrayElement() = 0;

    protected:
        IArchiveBase() = default;
        IArchiveBase(const IArchiveBase&) = default;
        IArchiveBase& operator=(const IArchiveBase&) = default;
        IArchiveBase(IArchiveBase&&) = default;
        IArchiveBase& operator=(IArchiveBase&&) = default;
    };

    /**
     * @brief Interface for serialization (writing data).
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

        [[deprecated("every field is written; remove the call")]] bool IsSkipDefaults() const {
            return false;
        }
        [[deprecated("every field is written; remove the call")]] void SetSkipDefaults(bool /*skip*/) {}

        // =================================================================
        // Primitive types serialization (as child elements)
        // =================================================================

        virtual void Write(StringView name, bool value) = 0;
        virtual void Write(StringView name, int8_t value) = 0;
        virtual void Write(StringView name, uint8_t value) = 0;
        virtual void Write(StringView name, int16_t value) = 0;
        virtual void Write(StringView name, uint16_t value) = 0;
        virtual void Write(StringView name, int32_t value) = 0;
        virtual void Write(StringView name, uint32_t value) = 0;
        virtual void Write(StringView name, int64_t value) = 0;
        virtual void Write(StringView name, uint64_t value) = 0;
        virtual void Write(StringView name, float value) = 0;
        virtual void Write(StringView name, double value) = 0;

        // =================================================================
        // String types serialization (as child elements)
        // =================================================================

        virtual void Write(StringView name, StringView value) = 0;
        virtual void Write(StringView name, const PoolString& value) = 0;

        // =================================================================
        // Attribute serialization (for XML attributes, inline for binary)
        // =================================================================

        virtual void WriteAttribute(StringView name, bool value) = 0;
        virtual void WriteAttribute(StringView name, int8_t value) = 0;
        virtual void WriteAttribute(StringView name, uint8_t value) = 0;
        virtual void WriteAttribute(StringView name, int16_t value) = 0;
        virtual void WriteAttribute(StringView name, uint16_t value) = 0;
        virtual void WriteAttribute(StringView name, int32_t value) = 0;
        virtual void WriteAttribute(StringView name, uint32_t value) = 0;
        virtual void WriteAttribute(StringView name, int64_t value) = 0;
        virtual void WriteAttribute(StringView name, uint64_t value) = 0;
        virtual void WriteAttribute(StringView name, float value) = 0;
        virtual void WriteAttribute(StringView name, double value) = 0;
        virtual void WriteAttribute(StringView name, StringView value) = 0;
        virtual void WriteAttribute(StringView name, const PoolString& value) = 0;

    protected:
        ISerializer() = default;
        ISerializer(const ISerializer&) = default;
        ISerializer& operator=(const ISerializer&) = default;
        ISerializer(ISerializer&&) = default;
        ISerializer& operator=(ISerializer&&) = default;
    };

}  // namespace BECore
