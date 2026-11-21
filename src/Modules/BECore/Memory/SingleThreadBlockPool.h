#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Memory/BlockPool.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief A BlockPool for the objects of one thread: `BE_POOLED_SINGLE_THREAD` makes it the pool of a type.
     *
     * No lock and no thread-local list, so it is the cheapest pool, and it is only right for objects whose whole life is on one thread. Debug asserts
     * that every call runs on the thread of the first one; Release trusts the contract.
     */
    class SingleThreadBlockPool {
    public:
        constexpr SingleThreadBlockPool(const char* name, size_t blockSize, size_t alignment) : _pool(name, blockSize, alignment) {}

        [[nodiscard]] void* Allocate() {
            CheckThread();
            return _pool.Allocate();
        }

        void Deallocate(void* ptr) {
            CheckThread();
            _pool.Deallocate(ptr);
        }

        [[nodiscard]] size_t GetLiveCount() const {
            return _pool.GetLiveCount();
        }
        [[nodiscard]] size_t GetPageCount() const {
            return _pool.GetPageCount();
        }
        [[nodiscard]] size_t GetBlockSize() const {
            return _pool.GetBlockSize();
        }

    private:
#if ENGINE_ENABLE_ASSERTS
        /// A value that differs per live thread: the address of one of its thread-locals (a std::thread::id atomic is not constant-initialisable)
        static uintptr_t CurrentThreadToken() {
            thread_local const char marker = 0;
            return reinterpret_cast<uintptr_t>(&marker);
        }

        void CheckThread() {
            const uintptr_t self = CurrentThreadToken();
            uintptr_t owner = 0;
            _owner.compare_exchange_strong(owner, self);
            const bool same = _owner.load() == self;
            ASSERT(same, "a single-thread pool is used from a second thread: use BE_POOLED for objects that cross threads");
        }
        std::atomic<uintptr_t> _owner{0};
#else
        void CheckThread() {}
#endif
        BlockPool _pool;
    };

}  // namespace BECore
