#pragma once

#include <BECore/File/Path.h>
#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/IDeserializer.h>
#include <expected>

namespace BECore {

    struct BinaryContent;

    namespace Tests {
        struct BinaryDeserializerAccess;
    }

    /**
     * @brief Reads the bytes of a binary document into @p out (resized to the file's size, one allocation)
     *
     * The one reading of a binary file: `BinaryDeserializer::LoadFromFile` and `ConfigManager` use it, so the checks do not drift
     * apart. Refused before the file is read when the path is not a regular file or the file is over
     * ReaderLimits::kMaxBinaryStreamBytes; the bytes are not validated here. On a refusal @p out is empty or untouched.
     *
     * @return nothing on success, else a message (a literal, valid for the whole run) that says what is wrong with the file
     */
    [[nodiscard]] std::expected<void, StringView> ReadBinaryDocumentFile(const Path& path, Vector<uint8_t>& out);

    /**
     * @brief Binary deserializer (reader) for the format written by BinarySerializer
     *
     * Reads version 1 (names inline), version 2 (a sorted string table; names and PoolString values are indices into it) and
     * version 3 (a name table and a pool section of the PoolString texts, each entry with a hash that this reader never uses).
     * LoadFromContent() is the one load that takes the PoolStrings ready-made (content mode: a cooked file the content loader
     * adopted into the pool); every other load hashes and interns the texts itself.
     * The whole buffer is validated when it is loaded (header, the tables, sizes, tags, indices, nesting), so reading from
     * a truncated or corrupt buffer fails in the Load*() call and never reads out of bounds afterwards.
     * Strings are interned only when asked for: names and String values never, a PoolString value on the first Read of one
     * (see Read(StringView, PoolString&)), so a file that is only peeked at grows no pool.
     * Errors follow XmlDeserializer: Read() reports a missing entry, ReadAttribute() and BeginObject()
     * stay silent (the field may be optional). A type mismatch is reported by both, because the data
     * is there but is not what the reader expects.
     */
    class BinaryDeserializer : public IDeserializer {
    public:
        BinaryDeserializer() = default;
        ~BinaryDeserializer() override = default;

        BinaryDeserializer(const BinaryDeserializer&) = delete;
        BinaryDeserializer& operator=(const BinaryDeserializer&) = delete;
        BinaryDeserializer(BinaryDeserializer&&) = default;
        BinaryDeserializer& operator=(BinaryDeserializer&&) = default;

        // =================================================================
        // Input
        // =================================================================

        /**
         * @brief Load and validate a buffer written by BinarySerializer, borrowing it (nothing is copied)
         *
         * The memory must stay alive and unchanged until the next Load*(), a failed load or the destruction of
         * the reader: the reader does not check it, and Read(String) / Read(PoolString) copy out of it only when asked.
         * Bytes that point into this reader's own storage are not supported. A moved reader keeps the view.
         * Refused like LoadFromBuffer().
         *
         * @return true if the buffer is valid. On failure an error is recorded and every later read fails.
         */
        bool LoadFromView(const uint8_t* data, size_t size);

        /**
         * @brief Load and validate a buffer written by BinarySerializer, copying the bytes
         *
         * For a caller that cannot promise the lifetime of the memory. A refused buffer is not copied.
         * Refused when it is over ReaderLimits::kMaxBinaryStreamBytes or holds an array of more than ReaderLimits::kMaxArrayElements.
         *
         * @return true if the buffer is valid. On failure an error is recorded and every later read fails.
         */
        bool LoadFromBuffer(const uint8_t* data, size_t size);
        bool LoadFromBuffer(const Vector<uint8_t>& data);

        /**
         * @brief Load a cooked content file that LoadContentFile / LoadContentBytes has read (content mode)
         *
         * Validates the compact stream as the other loads validate a full one (the size the header promises less the pool section's
         * entries, the name table, the head of the section, every entry; a PoolString index must be below `content.strings.size()`,
         * which must be the head's `count`) and reads a PoolString entry as `content.strings[index]`: nothing is interned, hashed or
         * copied. A content of version 1 or 2 has no ready strings and loads as LoadFromView() over its bytes.
         *
         * Borrows like LoadFromView(): `content.bytes` and `content.strings` must stay alive and unchanged until the next load,
         * a failed load or the destruction of the reader (moving the BinaryContent keeps both, the vectors' blocks move with it).
         *
         * @return true if the content is valid. On failure an error is recorded and every later read fails.
         */
        bool LoadFromContent(const BinaryContent& content);

        /**
         * @brief Load a file written by BinarySerializer::SaveToFile
         *
         * Reads the file straight into the buffer the reader keeps (one allocation of the file's size, no second copy)
         * and validates it there. Refused with an error, before the file is read, when the path is not a regular file
         * or the file is over the limit of LoadFromBuffer.
         *
         * @return true on success
         */
        bool LoadFromFile(const Path& path);

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
        /// Takes a String or a PoolString entry (a field that changes between the two keeps loading)
        bool Read(StringView name, String& outValue) override;

        /**
         * @brief Takes a String or a PoolString entry
         *
         * In content mode (LoadFromContent) the value is `content.strings[index]`: no intern happens. Otherwise the
         * first call after a load of version 2 or 3 interns, in one PoolString::InternBatch, every distinct text that a
         * PoolString entry of the file refers to (version 3: every entry of the pool section, hashed here, never trusted from
         * the file); later calls take the result. A text over ReaderLimits::kMaxPoolStringBytes
         * is not in the batch and is the error of AcceptPoolStringLength. A String entry (and any entry of version 1) is
         * interned when it is read, one text at a time.
         */
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

        bool HasKey(StringView name) const override;

        // Error tracking
        bool HasErrors() const override;
        const Vector<DeserializeError>& GetErrors() const override;
        void ClearErrors() override;

    protected:
        void AddError(StringView path, StringView message, int32_t line) override;

    private:
        friend struct Tests::BinaryDeserializerAccess;  // reads _hasDuplicateNames: the fast lookup is a test subject

        /// What Validate() found out; Open() keeps it
        struct Checked {
            uint16_t version = 0;
            BinaryFormat::StringTable table;  ///< Over the bytes that were validated (version 3: the name table); empty for version 1
            BinaryFormat::PoolSection pool;   ///< Version 3 only
            size_t rootBegin = 0;             ///< The first entry of the document
            bool hasDuplicateNames = false;
            /// One bit per table string that a PoolString entry refers to; empty when the file has none
            Vector<uint64_t> poolBits;
        };

        /// Byte range [begin, end) holding the entries of the current object or array element
        struct Scope {
            size_t begin = 0;
            size_t end = 0;
            /// Where the entry after the last match of FindEntry() starts (always the start of an entry, or end); the next lookup begins there
            mutable size_t cursor = 0;
        };

        /// The elements of an open array are not listed: the position of the next one is enough, because their sizes were validated on load
        struct ArrayContext {
            size_t next = 0;  // the u32 size of the next element
            size_t count = 0;
            size_t index = 0;
        };

        using TagPredicate = bool (*)(uint8_t tag);

        /**
         * @brief Find the entry called @p name in the current scope whose tag satisfies @p accepts
         *
         * A name written more than once resolves to the last entry. A reader asks for the fields in the order they were
         * written, so the scan starts at Scope::cursor (after the last match) and wraps round to the start of the scope; a
         * name that is not there costs one pass over the scope. That order says nothing about which entry wins when a name
         * repeats, so a file in which any scope holds one name twice in one kind of entry (scalar, object, array) is marked
         * by validation (_hasDuplicateNames) and always scans the whole scope forward, which keeps the last one.
         * In versions 2 and 3 the name is looked up once in the string table (not there: not found, no scan) and the entries are
         * compared by name index.
         *
         * @param moveCursor false for a question that is not a read (HasKey): the read that follows it would otherwise
         *        start after the answer and go all the way round
         */
        bool FindEntry(StringView name, TagPredicate accepts, bool moveCursor, BinaryFormat::EntryView& outEntry) const;

        /// Parse the entry at @p position of the open stream (either version); false only on a stream that was not validated
        bool ParseAt(size_t position, size_t scopeEnd, BinaryFormat::EntryView& outEntry) const;

        /// Find a scalar entry of any type. Reports "Element not found" only if @p reportMissing.
        bool FindScalarEntry(StringView name, bool reportMissing, BinaryFormat::EntryView& outEntry);

        /// Find a scalar entry and check its tag. Reports "Type mismatch" for another one.
        bool FindScalar(StringView name, uint8_t expectedTag, bool reportMissing, BinaryFormat::EntryView& outEntry);

        /// Find a String or PoolString entry and give its text (inline, or from the string table or the pool section)
        bool FindText(StringView name, bool reportMissing, BinaryFormat::EntryView& outEntry, StringView& outText);

        /// Interns the strings of the file that PoolString entries refer to, once per load
        void InternTableStrings();

        /// Version 3: builds _poolOffsets by one walk of the section the first time it is needed
        void EnsurePoolOffsets();

        /// Version 3: the text of entry @p index of the pool section, a view over the stream; @p index must be below the section's count
        StringView PoolEntryText(size_t index);

        bool ReadPoolString(StringView name, bool reportMissing, PoolString& outValue);

        template <typename T>
        bool ReadScalar(StringView name, T& outValue, bool reportMissing);

        /// The checks of every load, on bytes that are not kept yet. Records the error and returns false on a refusal.
        bool Validate(const uint8_t* data, size_t size, Checked& outChecked, const BinaryContent* content = nullptr);

        /// Makes @p data the stream that is read and opens the root scope; @p data holds the bytes @p checked was made from
        void Open(const uint8_t* data, size_t size, Checked&& checked);

        void ResetState();

        /// The bytes that are read: the caller's memory after LoadFromView(), _owned after the owning loads, null when nothing is loaded
        const uint8_t* _data = nullptr;
        size_t _size = 0;
        /// Backs _data only for LoadFromBuffer() and LoadFromFile(); a default move keeps _data valid because the heap block moves with it
        Vector<uint8_t> _owned;
        /// Set by Validate(): some scope of the stream holds a name twice in one kind of entry (see FindEntry)
        bool _hasDuplicateNames = false;
        uint16_t _version = 0;
        /// Versions 2 and 3: the entries name strings of _table by index
        bool _tabled = false;
        BinaryFormat::StringTable _table;
        /// Version 3: the pool section, and where each of its entries starts in the stream (built lazily, by EnsurePoolOffsets())
        BinaryFormat::PoolSection _pool;
        Vector<uint32_t> _poolOffsets;
        /// Bits of the table strings that PoolString entries refer to, from Validate(); consumed by InternTableStrings()
        Vector<uint64_t> _poolBits;
        /// The strings that InternTableStrings() interned: _pooled[i] is the text of table string _pooledIndices[i] (ascending);
        /// version 3 interns the whole section, so _pooled[i] is entry i and _pooledIndices stays empty
        Vector<uint32_t> _pooledIndices;
        Vector<PoolString> _pooled;
        bool _pooledReady = false;
        /// Content mode (LoadFromContent): the stream has the head of the pool section and no entries; they are the ready PoolStrings of
        /// the BinaryContent, borrowed. `_contentStrings` may be null when the section is empty, so the mode is its own flag
        bool _contentMode = false;
        const PoolString* _contentStrings = nullptr;
        Vector<Scope> _scopeStack;
        Vector<ArrayContext> _arrayStack;
        Vector<DeserializeError> _errors;
    };

}  // namespace BECore
