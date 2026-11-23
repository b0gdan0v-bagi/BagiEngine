#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// Why `PoolString::AdoptBlock` refused a block. A refusal changes nothing: no entry of the block is linked and the
    /// token is as it was. Completes the opaque declaration of `PoolString.h`.
    CORE_ENUM(PoolAdoptError, uint8_t,
              NotOwned,             ///< The token is empty (moved from, or already adopted), or was given out by another storage
              UsedBytesTooLarge,    ///< `usedBytes` is more than the capacity of the block
              UsedBytesNotAligned,  ///< `usedBytes` is not a multiple of 8, so the entries cannot end at it
              TruncatedEntry,       ///< A header, or the text and NUL its `size` announces, runs past `usedBytes`
              MissingNul,           ///< The byte after the text of an entry is not NUL
              WrongHash,            ///< With `verifyHashes`: the hash of an entry is not the hash of its text
              OutSizeMismatch,      ///< `out` is not empty and its size is not the number of entries of the block
              TextTooLong)          ///< With `maxTextBytes`: the `size` of an entry is more than the limit

    /// What an adopted block gave the pool
    struct PoolAdoptResult {
        size_t linkedStrings = 0;  ///< Entries that became the entry of their text
        size_t deadStrings = 0;    ///< Entries whose text was already in the pool (or is empty): left in the block, not linked
        size_t deadBytes = 0;      ///< Their aligned sizes
    };

    /// A block of pool memory for a loader to write entries into, then `PoolString::AdoptBlock`. A token, not an owner: the
    /// pool owns the memory from `AllocateBlock` on and never frees it, so a token may be dropped at any time. Move-only,
    /// and spent by a successful `AdoptBlock`, which leaves it empty (`Data()` null): from then on the entries are the
    /// pool's and nothing a caller holds can write into them. A moved-from or spent token is refused as `NotOwned`.
    class PoolBlock {
    public:
        PoolBlock() noexcept = default;
        PoolBlock(const PoolBlock&) = delete;
        PoolBlock& operator=(const PoolBlock&) = delete;
        PoolBlock(PoolBlock&& other) noexcept {
            *this = eastl::move(other);
        }
        PoolBlock& operator=(PoolBlock&& other) noexcept {
            _data = other._data;
            _capacity = other._capacity;
            _storageId = other._storageId;
            _index = other._index;
            other._data = nullptr;
            other._capacity = 0;
            other._storageId = 0;
            other._index = 0;
            return *this;
        }

        /// The memory: 8-aligned, `Capacity()` bytes, its content unspecified. Null for an empty token (moved from or adopted).
        [[nodiscard]] char* Data() const noexcept {
            return _data;
        }
        [[nodiscard]] size_t Capacity() const noexcept {
            return _capacity;
        }

    private:
        friend class Details::PoolStringStorage;

        PoolBlock(char* data, size_t capacity, uint64_t storageId, size_t index) noexcept : _data(data), _capacity(capacity), _storageId(storageId), _index(index) {}

        char* _data = nullptr;
        size_t _capacity = 0;
        uint64_t _storageId = 0;  ///< The storage that gave the block out
        size_t _index = 0;        ///< Its place in that storage's list of blocks
    };

}  // namespace BECore
