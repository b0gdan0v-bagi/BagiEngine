#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

namespace BECore {

    namespace Details {

        struct PoolStringEntry {
            const uint64_t hash;
            const uint32_t size;
            char data[1];

            constexpr PoolStringEntry(uint64_t h, uint32_t s) : hash(h), size(s), data{'\0'} {}

            [[nodiscard]] constexpr StringView ToStringView() const noexcept {
                return {&data[0], static_cast<size_t>(size)};
            }
        };

        // The text starts right after the 12 header bytes: the pool lays entries out in blocks by this offset
        static_assert(offsetof(PoolStringEntry, data) == 12);

        // The empty entry is a type of its own for MSVC
        struct EmptyEntry {
            PoolStringEntry header;
            constexpr EmptyEntry() : header(BECore::StringUtils::GetEmptyHash(), 0) {}
        };

        // Defined here, not inside the class, to avoid MSVC C2131
        inline constexpr EmptyEntry g_EmptyEntryStore{};

        /// Every entry starts at a multiple of this and takes a multiple of it
        inline constexpr size_t kPoolEntryAlignment = alignof(uint64_t);

        /// The longest text an entry can hold: `size` is 32 bits and the entry is padded past the NUL
        inline constexpr size_t kPoolEntryMaxTextBytes = UINT32_MAX - offsetof(PoolStringEntry, data) - kPoolEntryAlignment;

        /// Bytes an entry of a text of `textBytes` takes in a block: the header, the text and its NUL, padded to the alignment
        [[nodiscard]] constexpr size_t PoolEntryBytes(size_t textBytes) noexcept {
            return (offsetof(PoolStringEntry, data) + textBytes + 1 + kPoolEntryAlignment - 1) & ~(kPoolEntryAlignment - 1);
        }

        /// Writes an entry into `memory`, which is aligned to `kPoolEntryAlignment` and has `PoolEntryBytes(text.size())`
        /// bytes: the header, a copy of the text and its NUL. `hash` must be `StringUtils::GetHash(text)`. The pool, the
        /// tests and a cook that lays out entries in advance all build entries this way.
        inline PoolStringEntry* ConstructPoolEntry(void* memory, uint64_t hash, StringView text) {
            ASSERT(text.size() <= kPoolEntryMaxTextBytes);
            ASSERT(reinterpret_cast<uintptr_t>(memory) % kPoolEntryAlignment == 0);
            auto* entry = new (memory) PoolStringEntry(hash, static_cast<uint32_t>(text.size()));
            std::memcpy(entry->data, text.data(), text.size());
            entry->data[text.size()] = '\0';
            return entry;
        }

    }  // namespace Details

}  // namespace BECore
