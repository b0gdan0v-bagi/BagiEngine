#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/ISerializer.h>
#include <filesystem>

namespace BECore {

    /**
     * @brief Binary serializer (writer): compact, name-keyed, little-endian
     *
     * Same call sequence as XmlSerializer, so every type that serializes to XML serializes to binary.
     * Layout: BinaryFormat.h (version 2). Skipped fields (SetSkipDefaults, null pointers, empty optionals) are not
     * written; the reader finds the rest by name.
     *
     * The names of all entries and the text of PoolString values go into a string table, one entry per distinct
     * text, and the entries carry its index. A String value (`Write(name, StringView)`) is not tabled: it is written
     * inline, so it never reaches the pool of the reader. The table is sorted in the image, so the same document gives
     * the same bytes whatever order the texts were first written in.
     *
     * The working lists (the table, the open scopes, the offsets to patch) live in an arena the serializer owns, so a
     * document of hundreds of entries asks the heap for a few chunks and not for a node per text. It is the writer's own
     * arena and not a scratch scope because the writer outlives any call and is handed around (`ContentCook` passes it by
     * reference). The arena is not movable, so neither is the serializer: construct it in place.
     */
    class BinarySerializer : public ISerializer {
    public:
        BinarySerializer();
        ~BinarySerializer() override = default;

        BinarySerializer(const BinarySerializer&) = delete;
        BinarySerializer& operator=(const BinarySerializer&) = delete;
        BinarySerializer(BinarySerializer&&) = delete;
        BinarySerializer& operator=(BinarySerializer&&) = delete;

        /**
         * @brief The stream: header, string table and entries, as written so far
         *
         * Loadable by BinaryDeserializer once every Begin* has its matching End*. The image is built here, on the first
         * call after a write (the table is final only then), and kept until the next write. The reference is valid until
         * the next write or the destruction of the serializer.
         */
        const Vector<uint8_t>& GetData() const;

        /// Writes the bytes to a file through WriteFileAtomically: the folder is made and a reader sees the old file or the whole new one,
        /// never a half file. False if it cannot be written.
        bool SaveToFile(const std::filesystem::path& path) const;

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
         * @brief Write the tag and the name index of a new entry
         * @return false if an entry cannot start here (directly inside an array, outside an element)
         */
        bool BeginEntry(uint8_t tag, StringView name);

        template <typename T>
        void WriteScalar(StringView name, T value);

        void WriteString(StringView name, StringView value);

        void WritePoolString(StringView name, StringView value);

        /// Write the u32 table index of @p text (added to the table when new) and remember where it is, for GetData()
        void AppendTextIndex(StringView text);

        /// Start a size-prefixed scope (object or array element) after the entry header
        void OpenScope(bool isArray);

        /// Patch the size of the innermost scope and pop it
        void CloseScope(bool isArray);

        void Append(const void* data, size_t size);

        template <typename T>
        void AppendValue(T value);

        void PatchSizeField(size_t offset, size_t value);

        /// First member: it is destroyed last, after the containers that point into it
        LinearArena _arena;

        /// Entries only; the header and the table are put in front of them by GetData(). On the heap, not in the arena: it grows by
        /// doubling and the arena would keep every old block until the serializer ends. Measured on a document of 300 entries: the
        /// arena saves 4 of 9 allocations and costs 8 KiB more bytes.
        Vector<uint8_t> _buffer;
        ArenaVector<Scope> _scopes;

        /// Distinct texts of the table and their index in order of first use (the index the entries hold until GetData()); the keys
        /// are copies in _arena, since the name a caller passes may be a temporary
        ArenaHashMap<StringView, uint32_t> _texts;
        /// Offsets in _buffer of every u32 that holds a table index, patched to the sorted index in the image
        ArenaVector<size_t> _indexOffsets;

        mutable Vector<uint8_t> _image;
        mutable bool _imageIsStale = true;
    };

}  // namespace BECore
