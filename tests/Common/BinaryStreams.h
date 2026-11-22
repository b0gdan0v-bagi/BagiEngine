#pragma once

#include <Common/BinaryStreamBuilder.h>
#include <gtest/gtest.h>

namespace BECore::Tests::BinaryStreams {

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

}  // namespace BECore::Tests::BinaryStreams
