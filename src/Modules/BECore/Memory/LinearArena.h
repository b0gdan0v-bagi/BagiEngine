#pragma once

#include <BECore/Memory/ArenaStats.h>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace BECore {

    /**
     * @brief A bump allocator over a few big blocks: allocation moves a pointer, nothing is freed one by one, Reset() takes everything back.
     *
     * - **Chunks.** Memory comes from the heap (`::operator new`, so AllocTracker sees the few big blocks) in chunks, or the first chunk is a buffer the
     *   caller gives. A request that does not fit takes the next chunk kept from an earlier round, or a new one of `max(chunkSize, size + alignment)`
     *   bytes; a new chunk counts one overflow in the counters. A full caller's buffer falls back to the heap the same way: an overflow is counted,
     *   never fatal. Reset() and Rewind() keep every chunk; Release() frees all but the first.
     * - **No destructors.** Reset() runs none: only trivially destructible objects belong here (Allocate<T> checks it). An owner of a resource
     *   (a Vector on the default allocator, a String past its small buffer) must be destroyed before the arena forgets it.
     * - **Not thread-safe.** One arena belongs to one thread at a time; the counters are read from any thread (ArenaStats.h).
     * - **Marks.** Mark() / Rewind(mark) give back what was allocated after the mark (ScratchScope). Rewind in reverse order; a mark taken before a
     *   Reset() or Release() is invalid, and Debug asserts it.
     * - **Poisoning.** Freed memory is filled (Debug) or poisoned (ASan), `Memory/ArenaPoison.h`.
     *
     * Constant-initialised, so it can be a `constinit thread_local`: nothing is allocated or registered before the first Allocate.
     */
    class LinearArena {
    public:
        static constexpr size_t kDefaultChunkSize = 64 * 1024;
        static constexpr size_t kDefaultAlignment = alignof(std::max_align_t);

        /// A position in the arena that Rewind() goes back to
        class Mark {
        private:
            friend class LinearArena;
            void* _chunk = nullptr;  // the chunk being bumped, nullptr before the first allocation
            size_t _offset = 0;
            size_t _used = 0;
#if ENGINE_ENABLE_ASSERTS
            uint32_t _generation = 0;
#endif
        };

        /// An arena on the heap; the first chunk comes with the first Allocate
        constexpr explicit LinearArena(const char* name, size_t chunkSize = kDefaultChunkSize) : _name(name), _chunkSize(chunkSize) {}

        /// An arena whose first chunk is @p buffer of @p bufferSize bytes (a stack array, a static one); it must outlive the arena
        constexpr LinearArena(const char* name, std::byte* buffer, size_t bufferSize, size_t chunkSize = kDefaultChunkSize)
            : _name(name), _chunkSize(chunkSize), _first{bufferSize > 0 ? buffer : nullptr, bufferSize, 0, nullptr}, _capacity(bufferSize > 0 && buffer != nullptr ? bufferSize : 0),
              _chunkCount(_capacity > 0 ? 1 : 0) {}

        ~LinearArena();

        LinearArena(const LinearArena&) = delete;
        LinearArena& operator=(const LinearArena&) = delete;

        /// @p size bytes (at least one: a zero size gets a distinct address too) aligned to @p alignment (a power of two). Never null
        [[nodiscard]] void* Allocate(size_t size, size_t alignment = kDefaultAlignment);

        /// Storage for @p count objects of T, not constructed; T has no destructor to run on Reset()
        template <typename T>
        [[nodiscard]] T* Allocate(size_t count) {
            static_assert(std::is_trivially_destructible_v<T>, "an arena runs no destructor: T must be trivially destructible");
            return static_cast<T*>(AllocateArray(count, sizeof(T), alignof(T)));
        }

        [[nodiscard]] Mark GetMark() const;
        void Rewind(const Mark& mark);

        /// Gives back everything; every chunk stays for the next round. Invalidates the marks
        void Reset();

        /// Gives back everything and frees every chunk but the first. Invalidates the marks
        void Release();

        /// Bytes bumped since the last Reset, alignment gaps included
        [[nodiscard]] size_t GetBytesUsed() const {
            return _used;
        }
        /// Bytes of every chunk held
        [[nodiscard]] size_t GetCapacity() const {
            return _capacity;
        }
        [[nodiscard]] size_t GetChunkCount() const {
            return _chunkCount;
        }

    private:
        struct Chunk {
            std::byte* data = nullptr;
            size_t size = 0;
            size_t top = 0;  // the bumped bytes once the arena has moved on; the current chunk's are _offset
            Chunk* next = nullptr;
        };
        /// The header of a heap chunk sits in front of its data, padded so that the data starts as aligned as `new` returns
        static constexpr size_t kHeaderSize = (sizeof(Chunk) + kDefaultAlignment - 1) / kDefaultAlignment * kDefaultAlignment;

        [[nodiscard]] void* AllocateArray(size_t count, size_t elementSize, size_t alignment);
        [[nodiscard]] void* Commit(Chunk& chunk, size_t from, size_t start, size_t size);
        [[nodiscard]] void* AllocateInNewChunk(size_t size, size_t alignment);
        void TakeBack(Chunk* from, size_t offset);
        void FreeChunks(Chunk* head);
        void PublishCapacity();

        const char* _name;
        size_t _chunkSize;
        Chunk _first;               // the caller's buffer or the heap block of the first Allocate
        Chunk* _current = nullptr;  // nullptr: nothing bumped since Reset
        size_t _offset = 0;         // the bump position inside _current
        size_t _used = 0;
        size_t _capacity = 0;
        size_t _chunkCount = 0;
        bool _ownsFirst = false;  // _first.data is a heap block
        bool _statsClaimed = false;
        ArenaStatsSlot _stats;
#if ENGINE_ENABLE_ASSERTS
        uint32_t _generation = 1;
#endif
    };

}  // namespace BECore
