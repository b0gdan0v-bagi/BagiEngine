#pragma once

#include <BECore/Memory/BlockPool.h>
#include <BECore/Memory/ConcurrentBlockPool.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

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

    /// The pool of a type, never destroyed: a static destructor may free a pooled object after the pool's own destruction, and would write into freed pages
    template <typename TPool>
    union ImmortalPool {
        TPool pool;
        constexpr ImmortalPool(const char* name, size_t blockSize, size_t alignment) : pool(name, blockSize, alignment) {}
        ~ImmortalPool() {}  // NOLINT: the union must not destroy the pool
    };

}  // namespace BECore

/// @cond
#define BE_POOLED_WITH(T, PoolType)                                                                                                                                                                    \
    static PoolType& PooledPool() {                                                                                                                                                                    \
        static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "a pooled type is not over-aligned: the pool has no aligned operator new");                                                      \
        constinit static ::BECore::ImmortalPool<PoolType> storage(#T, sizeof(T), alignof(T));                                                                                                          \
        return storage.pool;                                                                                                                                                                           \
    }                                                                                                                                                                                                  \
    static void* operator new(::std::size_t size) {                                                                                                                                                    \
        return size == sizeof(T) ? PooledPool().Allocate() : ::operator new(size);                                                                                                                     \
    }                                                                                                                                                                                                  \
    static void* operator new(::std::size_t, void* where) noexcept {                                                                                                                                   \
        return where;                                                                                                                                                                                  \
    }                                                                                                                                                                                                  \
    static void operator delete(void* ptr, ::std::size_t size) {                                                                                                                                       \
        if (size == sizeof(T)) {                                                                                                                                                                       \
            PooledPool().Deallocate(ptr);                                                                                                                                                              \
        } else {                                                                                                                                                                                       \
            ::operator delete(ptr);                                                                                                                                                                    \
        }                                                                                                                                                                                              \
    }                                                                                                                                                                                                  \
    static void operator delete(void*, void*) noexcept {}
/// @endcond

/**
 * Opts the class @p T in to a pool of its own, for any thread: write it in the **public** section of the class body, once (the macro changes no access label; in a
 * private one `new T` does not compile). `New<T>`, `MakeIntrusive<T>`, `new T` and the `delete this` of the RefCounted bases then reach the pool, with no change at a call site.
 *
 * - `T::PooledPool()` is the pool (counters, tests). Its name in `GetPoolStats()` is the text of the argument.
 * - A class derived from T has another size and uses the heap, told apart by the size both operators get: opting the derived class in gives it a pool of its own.
 * - The pool is never destroyed. T must not be over-aligned (`static_assert`).
 * - A class-scope `operator new` hides the global ones: the placement form is declared, a `nothrow` or an aligned `new T` is not.
 */
#define BE_POOLED(T) BE_POOLED_WITH(T, ::BECore::ConcurrentBlockPool)

/// As BE_POOLED, over a SingleThreadBlockPool: only for objects made, used and freed on one thread (the Debug build asserts it)
#define BE_POOLED_SINGLE_THREAD(T) BE_POOLED_WITH(T, ::BECore::SingleThreadBlockPool)
