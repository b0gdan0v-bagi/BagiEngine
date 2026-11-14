#pragma once

#include <bit>

namespace BECore::BinaryFormat {

    static_assert(std::endian::native == std::endian::little, "Binary format is little-endian and is written with plain memcpy");
    static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<double>::is_iec559, "Binary format stores floats as IEEE 754 bit patterns");

    // =========================================================================
    // Layout
    //
    //   Stream  := Header Entry*
    //   Header  := 'B' 'E' 'B' 'N' | u16 version | u32 payloadSize     (payloadSize = bytes after the header)
    //   Entry   := u8 tag | u16 nameLen | name[nameLen] | Payload
    //   Payload := fixed-size scalar                                   (Bool..Double)
    //            | u32 len | bytes[len]                                (String)
    //            | u32 size | Entry*                                   (Object, size = bytes after the size field)
    //            | u32 size | u32 count | Element*                     (Array,  size = bytes after the size field)
    //   Element := u32 size | Entry*                                   (size = bytes after the size field)
    //
    // Everything is little-endian. Entries are matched by name, not by position, so fields can be skipped
    // (skip-defaults, null pointers, optionals) and reordered. A name that is written twice reads as the last write.
    // Write() and WriteAttribute() produce the same entry: there is no attribute/child split in binary.
    // =========================================================================

    inline constexpr char Magic[4] = {'B', 'E', 'B', 'N'};
    inline constexpr uint16_t CurrentVersion = 1;

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
    }  // namespace Tag

    /// Bool..Double and String: entries read by Read() / ReadAttribute().
    constexpr bool IsScalarTag(uint8_t tag) {
        return tag >= Tag::Bool && tag <= Tag::String;
    }

    constexpr bool IsObjectTag(uint8_t tag) {
        return tag == Tag::Object;
    }

    constexpr bool IsArrayTag(uint8_t tag) {
        return tag == Tag::Array;
    }

    /// Payload size of a fixed-size scalar, 0 for String, Object, Array and unknown tags.
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
     * @brief Header of one entry located inside a byte range
     */
    struct EntryView {
        uint8_t tag = 0;
        StringView name;
        size_t payloadBegin = 0;  // first byte after the name
        size_t end = 0;           // one past the last byte of the entry
    };

    /**
     * @brief Parse the entry that starts at @p position inside [position, scopeEnd)
     *
     * Checks the tag and that the name and the payload fit into the range, so it is safe on untrusted data.
     * It does not look inside Object / Array payloads.
     *
     * @return false if the entry is truncated or has an unknown tag
     */
    inline bool ParseEntry(const uint8_t* data, size_t position, size_t scopeEnd, EntryView& outEntry) {
        constexpr size_t entryHeaderSize = sizeof(uint8_t) + sizeof(uint16_t);
        if (position > scopeEnd || scopeEnd - position < entryHeaderSize) {
            return false;
        }

        const uint8_t tag = data[position];
        const size_t nameLength = Load<uint16_t>(data + position + sizeof(uint8_t));
        const size_t nameBegin = position + entryHeaderSize;
        if (scopeEnd - nameBegin < nameLength) {
            return false;
        }

        const size_t payloadBegin = nameBegin + nameLength;
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

        outEntry.tag = tag;
        outEntry.name = StringView(reinterpret_cast<const char*>(data + nameBegin), nameLength);
        outEntry.payloadBegin = payloadBegin;
        outEntry.end = payloadBegin + payloadSize;
        return true;
    }

}  // namespace BECore::BinaryFormat
