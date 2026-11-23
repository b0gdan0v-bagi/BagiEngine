#pragma once

#include <Common/BinaryStreamBuilder.h>
#include <gtest/gtest.h>

namespace BECore::Tests::BinaryStreams {

    namespace Detail {

        /// @p pool is the texts of the pool section of a version 3 stream, null for the other versions
        inline Vector<Entry> ReadScope(const uint8_t* data, size_t begin, size_t end, const BinaryFormat::StringTable* table, const Vector<String>* pool) {
            Vector<Entry> entries;
            size_t position = begin;
            while (position < end) {
                BinaryFormat::EntryView view;
                bool parsed = false;
                if (pool != nullptr) {
                    parsed = BinaryFormat::ParseEntryV3(data, position, end, *table, pool->size(), view);
                } else {
                    parsed = table != nullptr ? BinaryFormat::ParseEntryV2(data, position, end, *table, view) : BinaryFormat::ParseEntry(data, position, end, view);
                }
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
                        const uint32_t index = BinaryFormat::Load<uint32_t>(payload);
                        entry.text = pool != nullptr ? (*pool)[index] : String(table->Get(index));
                    } else {
                        std::memcpy(&entry.bits, payload, fixed);
                    }
                } else if (view.tag == BinaryFormat::Tag::String) {
                    entry.text = String(reinterpret_cast<const char*>(payload + sizeof(uint32_t)), BinaryFormat::Load<uint32_t>(payload));
                } else if (view.tag == BinaryFormat::Tag::Object) {
                    entry.children = ReadScope(data, view.payloadBegin + sizeof(uint32_t), view.end, table, pool);
                } else {
                    const size_t count = BinaryFormat::Load<uint32_t>(payload + sizeof(uint32_t));
                    size_t elementAt = view.payloadBegin + 2 * sizeof(uint32_t);
                    for (size_t i = 0; i < count; ++i) {
                        const size_t elementEnd = elementAt + sizeof(uint32_t) + BinaryFormat::Load<uint32_t>(data + elementAt);
                        entry.children.push_back(Element(ReadScope(data, elementAt + sizeof(uint32_t), elementEnd, table, pool)));
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
        if (version == BinaryFormat::TabledVersion || version == BinaryFormat::PooledVersion) {
            BinaryFormat::StringTable table;
            size_t tableEnd = 0;
            if (!BinaryFormat::ParseStringTable(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), table, tableEnd)) {
                ADD_FAILURE() << "ReadTree: the string table is not well formed";
                return {};
            }
            if (version == BinaryFormat::TabledVersion) {
                return Detail::ReadScope(bytes.data(), tableEnd, bytes.size(), &table, nullptr);
            }
            BinaryFormat::PoolSection section;
            size_t sectionEnd = 0;
            if (!BinaryFormat::ParsePoolSection(bytes.data(), tableEnd, bytes.size(), section, sectionEnd)) {
                ADD_FAILURE() << "ReadTree: the pool section is not well formed";
                return {};
            }
            Vector<String> pool;
            size_t at = section.entriesOffset;
            for (uint32_t i = 0; i < section.count; ++i) {
                const size_t size = BinaryFormat::Load<uint32_t>(bytes.data() + at + offsetof(Details::PoolStringEntry, size));
                pool.emplace_back(reinterpret_cast<const char*>(bytes.data() + at + offsetof(Details::PoolStringEntry, data)), size);
                at += Details::PoolEntryBytes(size);
            }
            return Detail::ReadScope(bytes.data(), sectionEnd, bytes.size(), &table, &pool);
        }
        return Detail::ReadScope(bytes.data(), BinaryFormat::HeaderSize, bytes.size(), nullptr, nullptr);
    }

}  // namespace BECore::Tests::BinaryStreams
