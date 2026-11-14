#pragma once

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
        virtual bool BeginObject(StringView name) = 0;

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
        virtual bool BeginArray(StringView name, StringView elementName, size_t& count) = 0;

        /**
         * @brief End the current array section
         */
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

        /**
         * @brief End the current array element
         */
        virtual void EndArrayElement() = 0;

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

        bool IsSkipDefaults() const {
            return _skipDefaults;
        }
        void SetSkipDefaults(bool skip) {
            _skipDefaults = skip;
        }

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

    private:
        bool _skipDefaults = false;
    };

}  // namespace BECore
