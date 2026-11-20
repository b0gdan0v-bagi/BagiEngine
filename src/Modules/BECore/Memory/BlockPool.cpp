#include "BlockPool.h"

#include <BECore/Memory/ArenaPoison.h>
#include <memory>
#include <new>

namespace BECore {

    namespace {

        /// What a freed block starts with: the link of the free list and, in Debug, a mark that a live block does not carry
        struct FreeHeader {
            void* next = nullptr;
            uintptr_t mark = 0;
        };
        static_assert(sizeof(FreeHeader) == 2 * sizeof(void*));

#if ENGINE_ENABLE_ASSERTS
        constexpr uintptr_t kFreeMark = static_cast<uintptr_t>(0xB10CF4EEDEADF4EEull);
#endif

        /// The header words of a freed block are the one place the pool reads inside poisoned memory
        FreeHeader ReadHeader(void* block) {
            FreeHeader header;
#if BE_ARENA_ASAN
            const bool poisoned = __asan_region_is_poisoned(block, sizeof(FreeHeader)) != nullptr;
            ArenaPoison::Unpoison(block, sizeof(FreeHeader));
#endif
            std::memcpy(&header, block, sizeof(FreeHeader));
#if BE_ARENA_ASAN
            if (poisoned) {
                ASAN_POISON_MEMORY_REGION(block, sizeof(FreeHeader));
            }
#endif
            return header;
        }

        /// Takes @p block back into the free state: the pattern and the poison over all of it, then the header words inside the poison
        void MarkFreed(void* block, size_t size, void* next) {
            ArenaPoison::Poison(block, size);
            FreeHeader header;
            header.next = next;
#if ENGINE_ENABLE_ASSERTS
            header.mark = kFreeMark;
#endif
            ArenaPoison::Unpoison(block, sizeof(FreeHeader));
            std::memcpy(block, &header, sizeof(FreeHeader));
#if BE_ARENA_ASAN
            ASAN_POISON_MEMORY_REGION(block, sizeof(FreeHeader));
#endif
        }

#if ENGINE_ENABLE_ASSERTS
        /// True when the bytes after the header still hold the pattern Debug filled in
        bool PatternIntact(const std::byte* block, size_t size) {
            for (size_t i = sizeof(FreeHeader); i < size; ++i) {
                if (block[i] != std::byte{ArenaPoison::kFreedPattern}) {
                    return false;
                }
            }
            return true;
        }
#endif

    }  // namespace

    BlockPool::~BlockPool() {
        ASSERT(_live == 0, "a pool died with live blocks");
        Page* page = _pages;
        while (page != nullptr) {
            Page* next = page->next;
            ArenaPoison::Unpoison(DataOf(page), _blocksPerPage * _stride);
            ::operator delete(page, std::align_val_t{std::max(_alignment, size_t{__STDCPP_DEFAULT_NEW_ALIGNMENT__})});
            page = next;
        }
        _stats.Release();
    }

    void* BlockPool::Allocate() {
        if (!_statsClaimed) {
            ClaimStats();
        }
        void* block = nullptr;
        if (_freeHead != nullptr) {
            block = TakeFree();
        } else {
            if (_bumpNext == _bumpEnd) {
                AddPage();
            }
            block = _bumpNext;
            _bumpNext += _stride;
            ArenaPoison::Unpoison(block, _stride);
        }
        ++_live;
        _stats.OnAllocate();
        return block;
    }

    void BlockPool::Deallocate(void* ptr) {
        if (ptr == nullptr) {
            return;
        }
#if ENGINE_ENABLE_ASSERTS
        const bool ours = IsBlock(ptr);
        ASSERT(ours, "the pointer is no block of this pool (foreign, or not on a block's start)");
        if (!ours) {
            return;
        }
        const bool freed = ReadHeader(ptr).mark == kFreeMark;
        ASSERT(!freed, "a block given back twice");
        if (freed) {
            return;
        }
#endif
        PushFree(ptr);
        --_live;
        _stats.OnFree();
    }

    std::byte* BlockPool::DataOf(Page* page) const {
        return reinterpret_cast<std::byte*>(page) + _pageHeaderBytes;
    }

    size_t BlockPool::PageBytes() const {
        return _pageHeaderBytes + _blocksPerPage * _stride;
    }

    void BlockPool::ClaimStats() {
        _statsClaimed = true;
        if (_counted) {
            _stats = PoolStatsSlot::Claim(_name, _stride, false);
        }
    }

    void BlockPool::AddPage() {
        ASSERT((_alignment & (_alignment - 1)) == 0, "the alignment of a pool is a power of two");
        const size_t alignment = std::max(_alignment, size_t{__STDCPP_DEFAULT_NEW_ALIGNMENT__});
        auto* memory = static_cast<std::byte*>(::operator new(PageBytes(), std::align_val_t{alignment}));
        Page* page = std::construct_at(reinterpret_cast<Page*>(memory), Page{_pages});
        _pages = page;
        ++_pageCount;
        _bumpNext = DataOf(page);
        _bumpEnd = _bumpNext + _blocksPerPage * _stride;
        ArenaPoison::Poison(_bumpNext, _blocksPerPage * _stride);
        _stats.OnPage(PageBytes());
    }

    void* BlockPool::TakeFree() {
        void* block = _freeHead;
        const FreeHeader header = ReadHeader(block);
#if ENGINE_ENABLE_ASSERTS
        const bool marked = header.mark == kFreeMark;
        ASSERT(marked, "a free block lost its mark: it was written after it was given back");
        // A link that was overwritten is not followed: the rest of the list is dropped (its blocks stay in their pages)
        _freeHead = marked ? header.next : nullptr;
#else
        _freeHead = header.next;
#endif
        ArenaPoison::Unpoison(block, _stride);
#if ENGINE_ENABLE_ASSERTS
        const bool intact = PatternIntact(static_cast<const std::byte*>(block), _stride);
        ASSERT(intact, "a free block was written after it was given back");
        std::memset(block, 0, sizeof(FreeHeader));
#endif
        return block;
    }

    void BlockPool::PushFree(void* block) {
        MarkFreed(block, _stride, _freeHead);
        _freeHead = block;
    }

#if ENGINE_ENABLE_ASSERTS
    bool BlockPool::IsBlock(const void* ptr) const {
        const auto address = reinterpret_cast<uintptr_t>(ptr);
        for (Page* page = _pages; page != nullptr; page = page->next) {
            const auto first = reinterpret_cast<uintptr_t>(DataOf(page));
            // Only the newest page is bumped partway: the blocks past the bump pointer were never handed out
            const auto end = page == _pages ? reinterpret_cast<uintptr_t>(_bumpNext) : first + _blocksPerPage * _stride;
            if (address >= first && address < end) {
                return (address - first) % _stride == 0;
            }
        }
        return false;
    }
#endif

}  // namespace BECore
