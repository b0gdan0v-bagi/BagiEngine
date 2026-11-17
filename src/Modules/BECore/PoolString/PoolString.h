#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/StringUtils.h>
#include <cstddef>
#include <cstdint>
#include <shared_mutex>

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

    }  // namespace Details

    class PoolString {
    public:
        /// Size of a standard block of the pool; a string whose entry does not fit one gets a block of its own.
        static constexpr size_t kBlockBytes = 128 * 1024;

        constexpr PoolString() noexcept : _entry(&Details::g_EmptyEntryStore.header) {}

        static PoolString Intern(StringView str);

        /// Returns the pooled string equal to `str`, or nullopt when no such string was interned. Never adds to the pool.
        [[nodiscard]] static Optional<PoolString> Find(StringView str);

        [[nodiscard]] constexpr uint64_t HashValue() const noexcept {
            return _entry->hash;
        }
        [[nodiscard]] constexpr StringView ToStringView() const noexcept {
            return _entry->ToStringView();
        }
        [[nodiscard]] constexpr const char* CStr() const noexcept {
            return _entry->data;
        }

        [[nodiscard]] constexpr bool Empty() const noexcept {
            return _entry->size == 0;
        }

        [[nodiscard]] constexpr size_t Length() const noexcept {
            return _entry->size;
        }

        [[nodiscard]] constexpr bool operator==(const PoolString& other) const noexcept {
            if consteval {
                return _entry->hash == other._entry->hash;
            }
            return _entry == other._entry;
        }
        [[nodiscard]] constexpr bool operator!=(const PoolString& other) const noexcept {
            return !(*this == other);
        }

    private:
        class Storage;
        friend class PoolString::Storage;

        using Entry = Details::PoolStringEntry;
        constexpr explicit PoolString(const Entry* entry) noexcept : _entry(entry) {}

        const Entry* _entry;
    };

    // The length and the hash live in the entry, so a PoolString stays one pointer and equality is a pointer compare.
    static_assert(sizeof(PoolString) == sizeof(void*));

}  // namespace BECore
