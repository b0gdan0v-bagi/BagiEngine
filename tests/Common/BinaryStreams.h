#pragma once

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Types/Types.h>
#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cstring>
#include <gtest/gtest.h>

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

    /// A value written with `Write(name, const PoolString&)`: a table index in version 2, inline in version 1
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
        /// every PoolString text of the document must be in it. Empty: the distinct texts, sorted
        Vector<String> table;
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

        inline void CollectTexts(const Vector<Entry>& entries, Vector<String>& texts) {
            for (const Entry& entry : entries) {
                texts.push_back(entry.name);
                if (entry.tag == BinaryFormat::Tag::PoolString) {
                    texts.push_back(entry.text);
                }
                if (entry.tag == BinaryFormat::Tag::Array) {
                    // An element has no name in the stream
                    for (const Entry& element : entry.children) {
                        CollectTexts(element.children, texts);
                    }
                } else {
                    CollectTexts(entry.children, texts);
                }
            }
        }

        struct Context {
            uint16_t version = 0;
            const Vector<String>* table = nullptr;

            uint32_t IndexOf(StringView text) const {
                for (size_t i = 0; i < table->size(); ++i) {
                    if (StringView((*table)[i]) == text) {
                        return static_cast<uint32_t>(i);
                    }
                }
                ADD_FAILURE() << "the table given to BinaryStreams::Build has no \"" << String(text).c_str() << "\"";
                return 0;
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
                    Put<uint32_t>(out, context.IndexOf(entry.text));
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

    }  // namespace Detail

    /// The bytes of a stream of the version in @p options with the entries of @p root in the scope of the document
    inline Vector<uint8_t> Build(const Vector<Entry>& root, const Options& options = {}) {
        Vector<uint8_t> out;
        out.insert(out.end(), reinterpret_cast<const uint8_t*>(BinaryFormat::Magic), reinterpret_cast<const uint8_t*>(BinaryFormat::Magic) + sizeof(BinaryFormat::Magic));
        Detail::Put<uint16_t>(out, options.version);
        Detail::Put<uint32_t>(out, 0);

        Vector<String> table = options.table;
        if (options.version == BinaryFormat::TabledVersion && table.empty()) {
            Detail::CollectTexts(root, table);
            eastl::sort(table.begin(), table.end(), [](const String& a, const String& b) { return BinaryFormat::CompareStrings(StringView(a), StringView(b)) < 0; });
            table.erase(eastl::unique(table.begin(), table.end()), table.end());
        }
        if (options.version == BinaryFormat::TabledVersion) {
            Detail::EmitTable(out, table);
        }

        Detail::Context context;
        context.version = options.version;
        context.table = &table;
        Detail::EmitEntries(out, root, context);
        BinaryFormat::Store<uint32_t>(out.data() + BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(out.size() - BinaryFormat::HeaderSize));
        return out;
    }

    namespace Detail {

        inline Vector<Entry> ReadScope(const uint8_t* data, size_t begin, size_t end, const BinaryFormat::StringTable* table) {
            Vector<Entry> entries;
            size_t position = begin;
            while (position < end) {
                BinaryFormat::EntryView view;
                const bool parsed = table != nullptr ? BinaryFormat::ParseEntryV2(data, position, end, *table, view) : BinaryFormat::ParseEntry(data, position, end, view);
                if (!parsed) {
                    ADD_FAILURE() << "ReadTree: the stream is not well formed";
                    break;
                }
                Entry entry;
                entry.name = String(view.name);
                entry.tag = view.tag;
                const uint8_t* payload = data + view.payloadBegin;
                if (const size_t fixed = BinaryFormat::FixedPayloadSize(view.tag); fixed != 0) {
                    if (view.tag == BinaryFormat::Tag::PoolString) {
                        entry.text = String(table->Get(BinaryFormat::Load<uint32_t>(payload)));
                    } else {
                        std::memcpy(&entry.bits, payload, fixed);
                    }
                } else if (view.tag == BinaryFormat::Tag::String) {
                    entry.text = String(reinterpret_cast<const char*>(payload + sizeof(uint32_t)), BinaryFormat::Load<uint32_t>(payload));
                } else if (view.tag == BinaryFormat::Tag::Object) {
                    entry.children = ReadScope(data, view.payloadBegin + sizeof(uint32_t), view.end, table);
                } else {
                    const size_t count = BinaryFormat::Load<uint32_t>(payload + sizeof(uint32_t));
                    size_t elementAt = view.payloadBegin + 2 * sizeof(uint32_t);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t elementEnd = elementAt + sizeof(uint32_t) + BinaryFormat::Load<uint32_t>(data + elementAt);
                        entry.children.push_back(Element(ReadScope(data, elementAt + sizeof(uint32_t), elementEnd, table)));
                        elementAt = elementEnd;
                    }
                }
                entries.push_back(eastl::move(entry));
                position = view.end;
            }
            return entries;
        }

    }  // namespace Detail

    /// The entries of a stream of either version, parsed with the format helpers and not with a reader: a document written by
    /// BinarySerializer becomes a tree that Build() writes again in the other version
    inline Vector<Entry> ReadTree(const Vector<uint8_t>& bytes) {
        if (bytes.size() < BinaryFormat::HeaderSize) {
            ADD_FAILURE() << "ReadTree: no header";
            return {};
        }
        const uint16_t version = BinaryFormat::Load<uint16_t>(bytes.data() + sizeof(BinaryFormat::Magic));
        if (version == BinaryFormat::TabledVersion) {
            BinaryFormat::StringTable table;
            size_t tableEnd = 0;
            if (!BinaryFormat::ParseStringTable(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), table, tableEnd)) {
                ADD_FAILURE() << "ReadTree: the string table is not well formed";
                return {};
            }
            return Detail::ReadScope(bytes.data(), tableEnd, bytes.size(), &table);
        }
        return Detail::ReadScope(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), nullptr);
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
