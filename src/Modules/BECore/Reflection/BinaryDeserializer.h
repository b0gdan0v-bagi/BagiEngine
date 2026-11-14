#pragma once

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/IDeserializer.h>

namespace BECore {

    /**
     * @brief Binary deserializer (reader) for the format written by BinarySerializer
     *
     * The whole buffer is validated when it is loaded (header, sizes, tags, nesting), so reading from
     * a truncated or corrupt buffer fails in LoadFromBuffer() and never reads out of bounds afterwards.
     * Errors follow XmlDeserializer: Read() reports a missing entry, ReadAttribute() and BeginObject()
     * stay silent (the field may be optional). A type mismatch is reported by both, because the data
     * is there but is not what the reader expects.
     *
     * @example
     * BinaryDeserializer deserializer;
     * deserializer.LoadFromFile("save.bin");
     * deserializer.BeginObject("player");
     * int32_t health = 0;
     * deserializer.ReadAttribute("health", health);
     * deserializer.EndObject();
     */
    class BinaryDeserializer : public IDeserializer {
    public:
        BinaryDeserializer() = default;
        ~BinaryDeserializer() override = default;

        // Non-copyable, movable
        BinaryDeserializer(const BinaryDeserializer&) = delete;
        BinaryDeserializer& operator=(const BinaryDeserializer&) = delete;
        BinaryDeserializer(BinaryDeserializer&&) = default;
        BinaryDeserializer& operator=(BinaryDeserializer&&) = default;

        // =================================================================
        // Input
        // =================================================================

        /**
         * @brief Load and validate a buffer written by BinarySerializer (the bytes are copied)
         *
         * Refused when it is over ReaderLimits::kMaxBinaryStreamBytes or holds an array of more than ReaderLimits::kMaxArrayElements.
         *
         * @return true if the buffer is valid. On failure an error is recorded and every later read fails.
         */
        bool LoadFromBuffer(const uint8_t* data, size_t size);
        bool LoadFromBuffer(const eastl::vector<uint8_t>& data);

        /**
         * @brief Load a file written by BinarySerializer::SaveToFile
         *
         * Refused with an error, before the file is read, when the path is not a regular file or the file is over the limit of LoadFromBuffer.
         *
         * @return true on success
         */
        bool LoadFromFile(const std::filesystem::path& path);

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
        bool Read(StringView name, String& outValue) override;
        bool Read(StringView name, PoolString& outValue) override;

        // Binary has no attributes: these read the same entries as Read(), but a missing entry is not an error
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
        bool ReadAttribute(StringView name, String& outValue) override;
        bool ReadAttribute(StringView name, PoolString& outValue) override;

        bool BeginObject(StringView name) override;
        void EndObject() override;

        bool BeginArray(StringView name, StringView elementName, size_t& count) override;
        void EndArray() override;
        bool BeginArrayElement() override;
        void EndArrayElement() override;

        // Error tracking
        bool HasErrors() const override;
        const eastl::vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        /// Byte range [begin, end) holding the entries of the current object or array element
        struct Scope {
            size_t begin = 0;
            size_t end = 0;
        };

        struct ArrayContext {
            eastl::vector<Scope> elements;
            size_t currentIndex = 0;
        };

        using TagPredicate = bool (*)(uint8_t tag);

        /**
         * @brief Find the entry called @p name in the current scope whose tag satisfies @p accepts
         *
         * A name written more than once resolves to the last entry.
         */
        bool FindEntry(StringView name, TagPredicate accepts, BinaryFormat::EntryView& outEntry) const;

        /// Find a scalar entry and check its tag. Reports "Element not found" only if @p reportMissing.
        bool FindScalar(StringView name, uint8_t expectedTag, bool reportMissing, BinaryFormat::EntryView& outEntry);

        template <typename T>
        bool ReadScalar(StringView name, T& outValue, bool reportMissing);

        bool ReadStringView(StringView name, bool reportMissing, StringView& outValue);

        void ResetState();

        eastl::vector<uint8_t> _buffer;
        eastl::vector<Scope> _scopeStack;
        eastl::vector<ArrayContext> _arrayStack;
        eastl::vector<DeserializeError> _errors;
    };

}  // namespace BECore
