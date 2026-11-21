#pragma once

#include <BECore/Types/Types.h>
#include <bit>
#include <cstddef>
#include <cstring>
#include <type_traits>

namespace BECore::BinaryFormat {

    static_assert(std::endian::native == std::endian::little, "Binary format is little-endian and is written with plain memcpy");
    static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559, "Binary format stores floats as IEEE 754 bit patterns");

    // =========================================================================
    // Layout
    //
    //   Header      := 'B' 'E' 'B' 'N' | u16 version | u32 payloadSize     (payloadSize = bytes after the header)
    //   Version 1   := Header Entry1*
    //   Entry1      := u8 tag | u16 nameLen | name[nameLen] | Payload
    //   Version 2   := Header StringTable Entry2*
    //   StringTable := u32 count | u32 charBytes | u32 end[count] | chars[charBytes]
    //                  string i = chars[(i == 0 ? 0 : end[i-1]), end[i]); the last end is charBytes (count 0: charBytes 0);
    //                  strictly ascending in CompareStrings order, so every text is there once
    //   Entry2      := u8 tag | u32 nameIndex | Payload                       (nameIndex < count)
    //   Payload     := fixed-size scalar                                   (Bool..Double)
    //                | u32 len | bytes[len]                                (String, inline in both versions)
    //                | u32 stringIndex                                     (PoolString, version 2 only; < count)
    //                | u32 size | Entry*                                   (Object, size = bytes after the size field)
    //                | u32 size | u32 count | Element*                     (Array,  size = bytes after the size field)
    //   Element     := u32 size | Entry*                                   (size = bytes after the size field)
    //
    // Everything is little-endian. Entries are matched by name, not by position, so fields can be skipped
    // (skip-defaults, null pointers, optionals) and reordered. A name that is written twice reads as the last write.
    // Write() and WriteAttribute() produce the same entry: there is no attribute/child split in binary.
    // The table holds the names of all entries and the text of the PoolString values; a String value is never in it.
    // =========================================================================

    inline constexpr char Magic[4] = {'B', 'E', 'B', 'N'};
    inline constexpr uint16_t LegacyVersion = 1;               ///< Names inline in every entry; still read, no longer written
    inline constexpr uint16_t TabledVersion = 2;               ///< Names and PoolString texts in a string table
    inline constexpr uint16_t CurrentVersion = TabledVersion;  ///< What BinarySerializer writes; a change of the byte layout moves it and keeps the old ones readable

    inline constexpr size_t PayloadSizeOffset = sizeof(Magic) + sizeof(uint16_t);
    inline constexpr size_t HeaderSize = PayloadSizeOffset + sizeof(uint32_t);

    /// Objects and arrays nested deeper than this are rejected when loading (bounds the validation recursion).
    inline constexpr size_t MaxDepth = 128;

    /// Entry tags. The numeric values are part of the format: never renumber, only append.
    namespace Tag {
        inline constexpr uint8_t Bool = 1;
        inline constexpr uint8_t Int8 = 2;
        inline constexpr uint8_t UInt8 = 3;
        inline constexpr uint8_t Int16 = 4;
        inline constexpr uint8_t UInt16 = 5;
        inline constexpr uint8_t Int32 = 6;
        inline constexpr uint8_t UInt32 = 7;
        inline constexpr uint8_t Int64 = 8;
        inline constexpr uint8_t UInt64 = 9;
        inline constexpr uint8_t Float = 10;
        inline constexpr uint8_t Double = 11;
        inline constexpr uint8_t String = 12;
        inline constexpr uint8_t Object = 13;
        inline constexpr uint8_t Array = 14;
        inline constexpr uint8_t PoolString = 15;  ///< Version 2 only: a u32 index into the string table
    }  // namespace Tag

    /// Bool..Double, String and PoolString: entries read by Read() / ReadAttribute().
    constexpr bool IsScalarTag(uint8_t tag) {
        return (tag >= Tag::Bool && tag <= Tag::String) || tag == Tag::PoolString;
    }

    /// String and PoolString are one kind for a reader: a field that changes between the two keeps loading.
    constexpr bool IsTextTag(uint8_t tag) {
        return tag == Tag::String || tag == Tag::PoolString;
    }

    constexpr bool IsObjectTag(uint8_t tag) {
        return tag == Tag::Object;
    }

    constexpr bool IsArrayTag(uint8_t tag) {
        return tag == Tag::Array;
    }

    /// Payload size of a fixed-size scalar (a PoolString is its u32 index), 0 for String, Object, Array and unknown tags.
    constexpr size_t FixedPayloadSize(uint8_t tag) {
        switch (tag) {
            case Tag::Bool:
            case Tag::Int8:
            case Tag::UInt8:
                return 1;
            case Tag::Int16:
            case Tag::UInt16:
                return 2;
            case Tag::Int32:
            case Tag::UInt32:
            case Tag::Float:
            case Tag::PoolString:
                return 4;
            case Tag::Int64:
            case Tag::UInt64:
            case Tag::Double:
                return 8;
            default:
                return 0;
        }
    }

    template <typename T>
    constexpr uint8_t TagFor() {
        if constexpr (std::is_same_v<T, bool>)
            return Tag::Bool;
        else if constexpr (std::is_same_v<T, int8_t>)
            return Tag::Int8;
        else if constexpr (std::is_same_v<T, uint8_t>)
            return Tag::UInt8;
        else if constexpr (std::is_same_v<T, int16_t>)
            return Tag::Int16;
        else if constexpr (std::is_same_v<T, uint16_t>)
            return Tag::UInt16;
        else if constexpr (std::is_same_v<T, int32_t>)
            return Tag::Int32;
        else if constexpr (std::is_same_v<T, uint32_t>)
            return Tag::UInt32;
        else if constexpr (std::is_same_v<T, int64_t>)
            return Tag::Int64;
        else if constexpr (std::is_same_v<T, uint64_t>)
            return Tag::UInt64;
        else if constexpr (std::is_same_v<T, float>)
            return Tag::Float;
        else if constexpr (std::is_same_v<T, double>)
            return Tag::Double;
        else
            static_assert(sizeof(T) != sizeof(T), "No binary tag for this type");
    }

    template <typename T>
    T Load(const uint8_t* source) {
        T value;
        std::memcpy(&value, source, sizeof(T));
        return value;
    }

    template <typename T>
    void Store(uint8_t* destination, T value) {
        std::memcpy(destination, &value, sizeof(T));
    }

    /**
     * @brief The order of the string table: unsigned bytes, a prefix first
     *
     * Not the locale and not the signedness of `char`: the writer sorts with it and the reader checks the table with it,
     * so the two cannot disagree.
     *
     * @return a negative number, zero or a positive number as @p a is before, equal to or after @p b
     */
    inline int CompareStrings(StringView a, StringView b) {
        const size_t common = a.size() < b.size() ? a.size() : b.size();
        if (common > 0) {
            const int bytes = std::memcmp(a.data(), b.data(), common);
            if (bytes != 0) {
                return bytes;
            }
        }
        if (a.size() == b.size()) {
            return 0;
        }
        return a.size() < b.size() ? -1 : 1;
    }

    /**
     * @brief The string table of a version 2 stream, as a view over the stream's bytes
     *
     * Holds offsets and the base pointer, so a stream that is copied to another place is rebased with Rebase()
     * instead of parsed again. It copies and owns nothing: the bytes must outlive the view.
     */
    struct StringTable {
        const uint8_t* base = nullptr;  ///< Start of the stream (the offsets are from here)
        size_t endsOffset = 0;          ///< Where end[0] is
        size_t charsOffset = 0;         ///< Where the characters start
        uint32_t count = 0;
        uint32_t charBytes = 0;

        /// The text of string @p index; @p index must be below count
        StringView Get(size_t index) const {
            const size_t begin = index == 0 ? 0 : Load<uint32_t>(base + endsOffset + (index - 1) * sizeof(uint32_t));
            const size_t end = Load<uint32_t>(base + endsOffset + index * sizeof(uint32_t));
            return StringView(reinterpret_cast<const char*>(base + charsOffset + begin), end - begin);
        }

        /// Binary search by CompareStrings; the table is sorted, so a text is there at most once
        Optional<uint32_t> Find(StringView text) const {
            size_t low = 0;
            size_t high = count;
            while (low < high) {
                const size_t middle = low + (high - low) / 2;
                const int order = CompareStrings(Get(middle), text);
                if (order == 0) {
                    return static_cast<uint32_t>(middle);
                }
                if (order < 0) {
                    low = middle + 1;
                } else {
                    high = middle;
                }
            }
            return {};
        }

        /// The same table over a copy of the stream that starts at @p newBase
        StringTable Rebase(const uint8_t* newBase) const {
            StringTable moved = *this;
            moved.base = newBase;
            return moved;
        }
    };

    /**
     * @brief Parse and check the string table that starts at @p position inside [position, limit)
     *
     * Checks that the sizes fit into the bytes left, that end[] never decreases and ends at charBytes, and that the
     * strings are strictly ascending (so none is there twice). Reads no more than the table's own bytes and allocates
     * nothing, so it is safe on untrusted data.
     *
     * @param outTableEnd one past the last byte of the table
     * @return false if the table is truncated or broken; the outputs are then unspecified
     */
    inline bool ParseStringTable(const uint8_t* data, size_t position, size_t limit, StringTable& outTable, size_t& outTableEnd) {
        constexpr size_t tableHeaderSize = 2 * sizeof(uint32_t);
        if (data == nullptr || position > limit || limit - position < tableHeaderSize) {
            return false;
        }

        const size_t count = Load<uint32_t>(data + position);
        const size_t charBytes = Load<uint32_t>(data + position + sizeof(uint32_t));
        const size_t endsOffset = position + tableHeaderSize;
        const size_t remaining = limit - endsOffset;
        if (count > remaining / sizeof(uint32_t)) {
            return false;
        }
        const size_t endsBytes = count * sizeof(uint32_t);
        if (charBytes > remaining - endsBytes) {
            return false;
        }

        StringTable table;
        table.base = data;
        table.endsOffset = endsOffset;
        table.charsOffset = endsOffset + endsBytes;
        table.count = static_cast<uint32_t>(count);
        table.charBytes = static_cast<uint32_t>(charBytes);

        size_t previousEnd = 0;
        for (size_t i = 0; i < count; ++i) {
            const size_t end = Load<uint32_t>(data + endsOffset + i * sizeof(uint32_t));
            if (end < previousEnd || end > charBytes) {
                return false;
            }
            previousEnd = end;
        }
        if (previousEnd != charBytes) {
            return false;
        }

        for (size_t i = 1; i < count; ++i) {
            if (CompareStrings(table.Get(i - 1), table.Get(i)) >= 0) {
                return false;
            }
        }

        outTable = table;
        outTableEnd = table.charsOffset + charBytes;
        return true;
    }

    /**
     * @brief Header of one entry located inside a byte range
     */
    struct EntryView {
        uint8_t tag = 0;
        StringView name;
        uint32_t nameIndex = 0;   // version 2: the name's index in the string table
        size_t payloadBegin = 0;  // first byte after the name
        size_t end = 0;           // one past the last byte of the entry
    };

    namespace Detail {

        /// Where the payload of an entry with @p tag that starts at @p payloadBegin ends, inside [payloadBegin, scopeEnd); false if it does not fit
        inline bool PayloadEnd(const uint8_t* data, uint8_t tag, size_t payloadBegin, size_t scopeEnd, size_t& outEnd) {
            const size_t remaining = scopeEnd - payloadBegin;
            size_t payloadSize = FixedPayloadSize(tag);
            if (payloadSize == 0) {
                if (tag != Tag::String && tag != Tag::Object && tag != Tag::Array) {
                    return false;
                }
                if (remaining < sizeof(uint32_t)) {
                    return false;
                }
                const size_t length = Load<uint32_t>(data + payloadBegin);
                if (length > remaining - sizeof(uint32_t)) {
                    return false;
                }
                payloadSize = sizeof(uint32_t) + length;
            }
            if (payloadSize > remaining) {
                return false;
            }
            outEnd = payloadBegin + payloadSize;
            return true;
        }

    }  // namespace Detail

    /**
     * @brief Parse the version 1 entry that starts at @p position inside [position, scopeEnd)
     *
     * Checks the tag and that the name and the payload fit into the range, so it is safe on untrusted data.
     * It does not look inside Object / Array payloads. Tag::PoolString is refused: a version 1 writer never wrote it.
     *
     * @return false if the entry is truncated or has an unknown tag
     */
    inline bool ParseEntry(const uint8_t* data, size_t position, size_t scopeEnd, EntryView& outEntry) {
        constexpr size_t entryHeaderSize = sizeof(uint8_t) + sizeof(uint16_t);
        if (position > scopeEnd || scopeEnd - position < entryHeaderSize) {
            return false;
        }

        const uint8_t tag = data[position];
        if (tag == Tag::PoolString) {
            return false;
        }
        const size_t nameLength = Load<uint16_t>(data + position + sizeof(uint8_t));
        const size_t nameBegin = position + entryHeaderSize;
        if (scopeEnd - nameBegin < nameLength) {
            return false;
        }

        const size_t payloadBegin = nameBegin + nameLength;
        size_t end = 0;
        if (!Detail::PayloadEnd(data, tag, payloadBegin, scopeEnd, end)) {
            return false;
        }

        outEntry.tag = tag;
        outEntry.name = StringView(reinterpret_cast<const char*>(data + nameBegin), nameLength);
        outEntry.nameIndex = 0;
        outEntry.payloadBegin = payloadBegin;
        outEntry.end = end;
        return true;
    }

    /**
     * @brief Parse the version 2 entry that starts at @p position inside [position, scopeEnd)
     *
     * As ParseEntry, and also checks that the name index (and, for Tag::PoolString, the string index) is below the
     * table's count; the name is resolved through @p table, which must outlive the entry.
     *
     * @return false if the entry is truncated, has an unknown tag or an index outside the table
     */
    inline bool ParseEntryV2(const uint8_t* data, size_t position, size_t scopeEnd, const StringTable& table, EntryView& outEntry) {
        constexpr size_t entryHeaderSize = sizeof(uint8_t) + sizeof(uint32_t);
        if (position > scopeEnd || scopeEnd - position < entryHeaderSize) {
            return false;
        }

        const uint8_t tag = data[position];
        const uint32_t nameIndex = Load<uint32_t>(data + position + sizeof(uint8_t));
        if (nameIndex >= table.count) {
            return false;
        }

        const size_t payloadBegin = position + entryHeaderSize;
        size_t end = 0;
        if (!Detail::PayloadEnd(data, tag, payloadBegin, scopeEnd, end)) {
            return false;
        }
        if (tag == Tag::PoolString && Load<uint32_t>(data + payloadBegin) >= table.count) {
            return false;
        }

        outEntry.tag = tag;
        outEntry.name = table.Get(nameIndex);
        outEntry.nameIndex = nameIndex;
        outEntry.payloadBegin = payloadBegin;
        outEntry.end = end;
        return true;
    }

}  // namespace BECore::BinaryFormat
