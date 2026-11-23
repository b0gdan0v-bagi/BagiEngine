#pragma once

#include <BECore/Memory/ConcurrentBlockPool.h>
#include <BECore/Memory/Immortal.h>
#include <BECore/Memory/SingleThreadBlockPool.h>
#include <cstddef>
#include <new>

/// @cond
#define BE_POOLED_WITH(T, PoolType)                                                                                                                                                                    \
    static PoolType& PooledPool() {                                                                                                                                                                    \
        static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "a pooled type is not over-aligned: the pool has no aligned operator new");                                                      \
        constinit static ::BECore::Immortal<PoolType> storage(#T, sizeof(T), alignof(T));                                                                                                              \
        return storage.value;                                                                                                                                                                          \
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
 * private one `new T` does not compile). `New<T>`, `new T` and the `delete this` of the RefCounted bases then reach the pool, with no change at a call site.
 *
 * - `T::PooledPool()` is the pool (counters, tests). Its name in `GetPoolStats()` is the text of the argument.
 * - A class derived from T has another size and uses the heap, told apart by the size both operators get: opting the derived class in gives it a pool of its own.
 * - The pool is never destroyed. T must not be over-aligned (`static_assert`).
 * - A class-scope `operator new` hides the global ones: the placement form is declared, a `nothrow` or an aligned `new T` is not.
 */
#define BE_POOLED(T) BE_POOLED_WITH(T, ::BECore::ConcurrentBlockPool)

/// As BE_POOLED, over a SingleThreadBlockPool: only for objects made, used and freed on one thread (the Debug build asserts it)
#define BE_POOLED_SINGLE_THREAD(T) BE_POOLED_WITH(T, ::BECore::SingleThreadBlockPool)
