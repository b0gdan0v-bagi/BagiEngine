#pragma once

#include <BECore/Memory/PoolStats.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief A pool of fixed-size blocks: an intrusive free list in front of a bump over the newest page.
     *
     * - **Pages.** Memory comes from `::operator new` (the aligned form) in pages of about `kTargetPageBytes`, so AllocTracker sees a few big blocks and not
     *   each object. **Pages go back to the heap only when the pool is destroyed**: a freed block is kept for the next Allocate.
     * - **Not thread-safe.** The counters are read from any thread (PoolStats.h); ConcurrentBlockPool is the kind for objects freed on another thread.
     * - **Size.** The block is the requested size rounded up to the alignment and to at least two words (the free list's link and its mark), so
     *   GetBlockSize() can be larger than what was asked for.
     * - **Checks** (Memory/CLAUDE.md, "Pools"). Debug fills a freed block with `ArenaPoison::kFreedPattern` except its link and mark, and asserts a double free, a
     *   pointer that is no block of this pool, and a block written after it was freed. An ASan build poisons freed and never used blocks. Release does neither.
     * - A pool that dies with live blocks is a bug (Debug asserts); one that never allocated holds and registers nothing, so it can be `constinit`.
     */
    class BlockPool {
    public:
        static constexpr size_t kTargetPageBytes = 16 * 1024;

        /**
         * @param name a string literal: the counters registry keeps the pointer
         * @param blockSize bytes the caller needs
         * @param alignment a power of two
         * @param blocksPerPage zero: as many as fit in about `kTargetPageBytes`
         * @param counted false for a pool that a ConcurrentBlockPool wraps and counts itself
         */
        constexpr BlockPool(const char* name, size_t blockSize, size_t alignment, size_t blocksPerPage = 0, bool counted = true)
            : _name(name), _alignment(std::max(alignment, alignof(void*))), _stride(RoundUp(std::max(blockSize, kMinBlockSize), _alignment)), _pageHeaderBytes(RoundUp(sizeof(void*), _alignment)),
              _blocksPerPage(blocksPerPage != 0 ? blocksPerPage : (kTargetPageBytes > _pageHeaderBytes + _stride ? (kTargetPageBytes - _pageHeaderBytes) / _stride : 1)), _counted(counted) {}

        ~BlockPool();

        BlockPool(const BlockPool&) = delete;
        BlockPool& operator=(const BlockPool&) = delete;

        /// A block of GetBlockSize() bytes aligned as asked. Never null; the contents are not initialised
        [[nodiscard]] void* Allocate();

        /// Gives a block of this pool back; a null pointer does nothing, as in `delete`
        void Deallocate(void* ptr);

        /// Blocks handed out and not given back
        [[nodiscard]] size_t GetLiveCount() const {
            return _live;
        }
        [[nodiscard]] size_t GetPageCount() const {
            return _pageCount;
        }
        /// Bytes of one block, the stride of a page
        [[nodiscard]] size_t GetBlockSize() const {
            return _stride;
        }

    private:
        static constexpr size_t kMinBlockSize = 2 * sizeof(void*);

        struct Page {
            Page* next = nullptr;
        };

        static constexpr size_t RoundUp(size_t value, size_t alignment) {
            return (value + alignment - 1) / alignment * alignment;
        }

        [[nodiscard]] std::byte* DataOf(Page* page) const;
        [[nodiscard]] size_t PageBytes() const;
        void AddPage();
        void ClaimStats();
        [[nodiscard]] void* TakeFree();
        void PushFree(void* block);
#if ENGINE_ENABLE_ASSERTS
        [[nodiscard]] bool IsBlock(const void* ptr) const;
#endif

        const char* _name;
        size_t _alignment;
        size_t _stride;
        size_t _pageHeaderBytes;
        size_t _blocksPerPage;
        bool _counted;
        bool _statsClaimed = false;
        void* _freeHead = nullptr;
        Page* _pages = nullptr;  // the newest first
        std::byte* _bumpNext = nullptr;
        std::byte* _bumpEnd = nullptr;
        size_t _live = 0;
        size_t _pageCount = 0;
        PoolStatsSlot _stats;
    };

}  // namespace BECore
