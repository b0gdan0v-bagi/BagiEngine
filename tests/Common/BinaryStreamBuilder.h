#pragma once

// The writer of test streams of the three versions, with no gtest: the unit tests and the fuzz seeds share it (BinaryStreams.h adds the reader)

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Types/Types.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace BECore::Tests::BinaryStreams {

    /**
     * One entry of a document as a test describes it: a name, a tag of BinaryFormat::Tag and a value.
     *
     * An Object holds its entries in `children`; an Array holds one Object-tagged entry per element (see Element()), whose
     * `children` are the element's entries. The list of a scope keeps its order and may repeat a name.
     */
    struct Entry {
        String name;
        uint8_t tag = 0;
        uint64_t bits = 0;  ///< A number or a bool: the low bytes of the value, as many as its tag has
        String text;        ///< String and PoolString
        Vector<Entry> children;
    };

    template <typename T>
    Entry Number(StringView name, T value) {
        Entry entry;
        entry.name = String(name);
        entry.tag = BinaryFormat::TagFor<T>();
        static_assert(sizeof(T) <= sizeof(uint64_t));
        std::memcpy(&entry.bits, &value, sizeof(T));
        return entry;
    }

    /// A value written with `Write(name, StringView)`: inline in both versions
    inline Entry Text(StringView name, StringView text) {
        Entry entry;
        entry.name = String(name);
        entry.tag = BinaryFormat::Tag::String;
        entry.text = String(text);
        return entry;
    }

    /// A value written with `Write(name, const PoolString&)`: a table index in version 2, a pool section index in version 3, inline in version 1
    inline Entry Pooled(StringView name, StringView text) {
        Entry entry = Text(name, text);
        entry.tag = BinaryFormat::Tag::PoolString;
        return entry;
    }

    inline Entry Object(StringView name, Vector<Entry> children) {
        Entry entry;
        entry.name = String(name);
        entry.tag = BinaryFormat::Tag::Object;
        entry.children = eastl::move(children);
        return entry;
    }

    /// One element of an array: the entries of its scope
    inline Entry Element(Vector<Entry> children) {
        return Object("", eastl::move(children));
    }

    inline Entry Array(StringView name, Vector<Entry> elements) {
        Entry entry;
        entry.name = String(name);
        entry.tag = BinaryFormat::Tag::Array;
        entry.children = eastl::move(elements);
        return entry;
    }

    struct Options {
        uint16_t version = BinaryFormat::TabledVersion;
        /// Version 2: the strings of the table, in this order, as given (so a test can make it unsorted or repeat a text); every name and
        /// every PoolString text of the document must be in it. Version 3: the same for the name table, which holds names only.
        /// Empty: the distinct texts, sorted
        Vector<String> table;
        /// Version 3: the texts of the pool section, in this order, as given (so a test can make it unsorted or repeat a text); every PoolString
        /// text of the document must be in it. Empty: the distinct texts, sorted
        Vector<String> pool;
        /// Version 3: the hash written for the entry with this text instead of its real one (every other entry keeps the real one)
        Vector<Pair<String, uint64_t>> hashOverrides;
        /// Version 3: written instead of `Details::PoolHashFunctionId()`, and instead of the checksum of the entries as they are written
        Optional<uint32_t> hashFunction;
        Optional<uint64_t> checksum;
    };

    namespace Detail {

        template <typename T>
        void Put(Vector<uint8_t>& out, T value) {
            const size_t at = out.size();
            out.resize(at + sizeof(T));
            BinaryFormat::Store<T>(out.data() + at, value);
        }

        inline void PutText(Vector<uint8_t>& out, StringView text) {
            out.insert(out.end(), reinterpret_cast<const uint8_t*>(text.data()), reinterpret_cast<const uint8_t*>(text.data()) + text.size());
        }

        /// The names go to @p names; the PoolString texts go to @p pool, or to @p names too when @p pool is null (version 2)
        inline void CollectTexts(const Vector<Entry>& entries, Vector<String>& names, Vector<String>* pool) {
            for (const Entry& entry : entries) {
                names.push_back(entry.name);
                if (entry.tag == BinaryFormat::Tag::PoolString) {
                    (pool != nullptr ? *pool : names).push_back(entry.text);
                }
                if (entry.tag == BinaryFormat::Tag::Array) {
                    // An element has no name in the stream
                    for (const Entry& element : entry.children) {
                        CollectTexts(element.children, names, pool);
                    }
                } else {
                    CollectTexts(entry.children, names, pool);
                }
            }
        }

        inline void SortDistinct(Vector<String>& texts) {
            eastl::sort(texts.begin(), texts.end(), [](const String& a, const String& b) { return BinaryFormat::CompareStrings(StringView(a), StringView(b)) < 0; });
            texts.erase(eastl::unique(texts.begin(), texts.end()), texts.end());
        }

        inline uint32_t IndexIn(const Vector<String>& list, StringView text) {
            for (size_t i = 0; i < list.size(); ++i) {
                if (StringView(list[i]) == text) {
                    return static_cast<uint32_t>(i);
                }
            }
            // A mistake of the test that builds the stream, and the header has no gtest to report it to (the fuzz seeds use it too)
            std::fprintf(stderr, "the table given to BinaryStreams::Build has no \"%.*s\"\n", static_cast<int>(text.size()), text.data());
            std::abort();
        }

        struct Context {
            uint16_t version = 0;
            const Vector<String>* table = nullptr;
            const Vector<String>* pool = nullptr;  ///< Version 3

            uint32_t IndexOf(StringView text) const {
                return IndexIn(*table, text);
            }

            uint32_t PoolIndexOf(StringView text) const {
                return IndexIn(*pool, text);
            }
        };

        inline void EmitEntries(Vector<uint8_t>& out, const Vector<Entry>& entries, const Context& context);

        inline void EmitEntry(Vector<uint8_t>& out, const Entry& entry, const Context& context) {
            // Version 1 has no PoolString tag: such a value is an inline String there
            const uint8_t tag = (entry.tag == BinaryFormat::Tag::PoolString && context.version == BinaryFormat::LegacyVersion) ? BinaryFormat::Tag::String : entry.tag;
            Put<uint8_t>(out, tag);
            if (context.version == BinaryFormat::LegacyVersion) {
                Put<uint16_t>(out, static_cast<uint16_t>(entry.name.size()));
                PutText(out, entry.name);
            } else {
                Put<uint32_t>(out, context.IndexOf(entry.name));
            }

            if (const size_t fixed = BinaryFormat::FixedPayloadSize(tag); fixed != 0) {
                if (tag == BinaryFormat::Tag::PoolString) {
                    Put<uint32_t>(out, context.version == BinaryFormat::PooledVersion ? context.PoolIndexOf(entry.text) : context.IndexOf(entry.text));
                } else {
                    const size_t at = out.size();
                    out.resize(at + fixed);
                    std::memcpy(out.data() + at, &entry.bits, fixed);
                }
                return;
            }
            if (tag == BinaryFormat::Tag::String) {
                Put<uint32_t>(out, static_cast<uint32_t>(entry.text.size()));
                PutText(out, entry.text);
                return;
            }

            const size_t sizeAt = out.size();
            Put<uint32_t>(out, 0);
            if (tag == BinaryFormat::Tag::Array) {
                Put<uint32_t>(out, static_cast<uint32_t>(entry.children.size()));
                for (const Entry& element : entry.children) {
                    const size_t elementAt = out.size();
                    Put<uint32_t>(out, 0);
                    EmitEntries(out, element.children, context);
                    BinaryFormat::Store<uint32_t>(out.data() + elementAt, static_cast<uint32_t>(out.size() - elementAt - sizeof(uint32_t)));
                }
            } else {
                EmitEntries(out, entry.children, context);
            }
            BinaryFormat::Store<uint32_t>(out.data() + sizeAt, static_cast<uint32_t>(out.size() - sizeAt - sizeof(uint32_t)));
        }

        inline void EmitEntries(Vector<uint8_t>& out, const Vector<Entry>& entries, const Context& context) {
            for (const Entry& entry : entries) {
                EmitEntry(out, entry, context);
            }
        }

        inline void EmitTable(Vector<uint8_t>& out, const Vector<String>& table) {
            size_t charBytes = 0;
            for (const String& text : table) {
                charBytes += text.size();
            }
            Put<uint32_t>(out, static_cast<uint32_t>(table.size()));
            Put<uint32_t>(out, static_cast<uint32_t>(charBytes));
            size_t end = 0;
            for (const String& text : table) {
                end += text.size();
                Put<uint32_t>(out, static_cast<uint32_t>(end));
            }
            for (const String& text : table) {
                PutText(out, text);
            }
        }

        inline void EmitPool(Vector<uint8_t>& out, const Vector<String>& pool, const Options& options) {
            size_t entryBytes = 0;
            for (const String& text : pool) {
                entryBytes += Details::PoolEntryBytes(text.size());
            }
            // The entries are built in a block of their own: ConstructPoolEntry wants 8-aligned memory and the section starts at any offset of the stream
            Vector<uint8_t> entries(entryBytes, 0);
            size_t at = 0;
            for (const String& text : pool) {
                uint64_t hash = StringUtils::GetHash(text);
                for (const Pair<String, uint64_t>& override : options.hashOverrides) {
                    if (StringView(override.first) == StringView(text)) {
                        hash = override.second;
                    }
                }
                Details::ConstructPoolEntry(entries.data() + at, hash, text);
                at += Details::PoolEntryBytes(text.size());
            }
            Put<uint32_t>(out, static_cast<uint32_t>(pool.size()));
            Put<uint32_t>(out, static_cast<uint32_t>(entryBytes));
            Put<uint32_t>(out, options.hashFunction.value_or(Details::PoolHashFunctionId()));
            Put<uint32_t>(out, 0);
            Put<uint64_t>(out, options.checksum.value_or(BinaryFormat::PoolSectionChecksum(entries.data(), entryBytes)));
            out.insert(out.end(), entries.begin(), entries.end());
        }

    }  // namespace Detail

    /// The bytes of a stream of the version in @p options with the entries of @p root in the scope of the document
    inline Vector<uint8_t> Build(const Vector<Entry>& root, const Options& options = {}) {
        Vector<uint8_t> out;
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(BinaryFormat::Magic), reinterpret_cast<const uint8_t*>(BinaryFormat::Magic) + sizeof(BinaryFormat::Magic));
        Detail::Put<uint16_t>(out, options.version);
        Detail::Put<uint32_t>(out, 0);

        const bool tabled = options.version != BinaryFormat::LegacyVersion;
        const bool pooled = options.version == BinaryFormat::PooledVersion;
        Vector<String> table = options.table;
        Vector<String> pool = options.pool;
        if (tabled && (table.empty() || (pooled && pool.empty()))) {
            Vector<String> names;
            Vector<String> texts;
            Detail::CollectTexts(root, names, pooled ? &texts : nullptr);
            if (table.empty()) {
                table = eastl::move(names);
                Detail::SortDistinct(table);
            }
            if (pooled && pool.empty()) {
                pool = eastl::move(texts);
                Detail::SortDistinct(pool);
            }
        }
        if (tabled) {
            Detail::EmitTable(out, table);
        }
        if (pooled) {
            Detail::EmitPool(out, pool, options);
        }

        Detail::Context context;
        context.version = options.version;
        context.table = &table;
        context.pool = &pool;
        Detail::EmitEntries(out, root, context);
        BinaryFormat::Store<uint32_t>(out.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(out.size() - BinaryFormat::HeaderSize));
        return out;
    }

    /// Where the parts of a version 2 stream start; counts are read from the bytes, so call it on a stream whose table sizes are right
    struct Layout {
        size_t countOffset = BinaryFormat::HeaderSize;
        size_t charBytesOffset = BinaryFormat::HeaderSize + sizeof(uint32_t);
        size_t endsOffset = BinaryFormat::HeaderSize + 2 * sizeof(uint32_t);
        size_t charsOffset = 0;
        size_t entriesOffset = 0;
    };

    inline Layout LayoutOf(const Vector<uint8_t>& bytes) {
        Layout layout;
        const size_t count = BinaryFormat::Load<uint32_t>(bytes.data() + layout.countOffset);
        const size_t charBytes = BinaryFormat::Load<uint32_t>(bytes.data() + layout.charBytesOffset);
        layout.charsOffset = layout.endsOffset + count * sizeof(uint32_t);
        layout.entriesOffset = layout.charsOffset + charBytes;
        return layout;
    }

}  // namespace BECore::Tests::BinaryStreams
