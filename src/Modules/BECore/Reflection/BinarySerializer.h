#pragma once

#include <BECore/Reflection/ISerializer.h>

namespace BECore {

    /**
     * @brief Binary serializer (writer): compact, name-keyed, little-endian
     *
     * Same call sequence as XmlSerializer, so every type that serializes to XML serializes to binary.
     * The layout is described in BinaryFormat.h. Fields that are skipped (SetSkipDefaults, null pointers,
     * empty optionals) are simply not written; the reader finds the rest by name.
     *
     * @example
     * BinarySerializer serializer;
     * serializer.BeginObject("player");
     * serializer.WriteAttribute("health", 100);
     * serializer.EndObject();
     * serializer.SaveToFile("save.bin");
     */
    class BinarySerializer : public ISerializer {
    public:
        BinarySerializer();
        ~BinarySerializer() override = default;

        // Non-copyable, movable
        BinarySerializer(const BinarySerializer&) = delete;
        BinarySerializer& operator=(const BinarySerializer&) = delete;
        BinarySerializer(BinarySerializer&&) = default;
        BinarySerializer& operator=(BinarySerializer&&) = default;

        // =================================================================
        // Output
        // =================================================================

        /**
         * @brief Bytes written so far, header included
         *
         * Loadable by BinaryDeserializer once every Begin* has its matching End*.
         */
        const eastl::vector<uint8_t>& GetData() const {
            return _buffer;
        }

        /**
         * @brief Save the bytes to a file
         * @return true on success
         */
        bool SaveToFile(const std::filesystem::path& path) const;

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

        // Binary has no attributes: these write the same entries as Write()
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

        /**
         * @brief Begin an array
         *
         * @p count is ignored: the stored count is the number of BeginArrayElement calls, so skipped
         * (null) elements do not leave holes.
         */
        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

    private:
        struct Scope {
            size_t sizeOffset = 0;  // position of the u32 size field to patch when the scope ends
            uint32_t elementCount = 0;
            bool isArray = false;
        };

        /**
         * @brief Write the tag and the name of a new entry
         * @return false if an entry cannot start here (directly inside an array, outside an element)
         */
        bool BeginEntry(uint8_t tag, StringView name);

        template <typename T>
        void WriteScalar(StringView name, T value);

        void WriteString(StringView name, StringView value);

        /// Start a size-prefixed scope (object or array element) after the entry header
        void OpenScope(bool isArray);

        /// Patch the size of the innermost scope and pop it
        void CloseScope(bool isArray);

        void Append(const void* data, size_t size);

        template <typename T>
        void AppendValue(T value);

        void PatchSizeField(size_t offset, size_t value);

        eastl::vector<uint8_t> _buffer;
        eastl::vector<Scope> _scopes;
    };

}  // namespace BECore
