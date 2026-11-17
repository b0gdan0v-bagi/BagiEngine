#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>

namespace BECore {

    /// A range of memory given by an `IPoolBlockSource`; an empty one (`data == nullptr`) means "no more".
    struct PoolMemory {
        void* data = nullptr;
        size_t bytes = 0;
    };

    /// Where the pool gets the memory its entries are written into.
    ///
    /// Contract of `Allocate(bytes)`: `bytes` is a multiple of 8. The range is aligned to 8, writable end to end and
    /// valid until the source is destroyed (the pool never gives memory back, so a source has no `Free`). Its content is
    /// not specified: the pool writes every byte it uses. A source that has nothing more to give (the reservation is
    /// used up or the system refused) returns an empty `PoolMemory`; it never asserts and never aborts, because the pool
    /// answers an empty result by taking a block from the heap, so `Intern` never fails.
    ///
    /// A source is not thread-safe: the pool calls it under its unique lock. It outlives the pool that uses it.
    class IPoolBlockSource {
    public:
        virtual ~IPoolBlockSource() = default;

        [[nodiscard]] virtual PoolMemory Allocate(size_t bytes) = 0;
    };

    /// One heap array per call, kept until the source is destroyed. The source of wasm (no address space to reserve)
    /// and the one tests take.
    [[nodiscard]] UniquePtr<IPoolBlockSource> MakeHeapBlockSource();

    inline constexpr size_t kDefaultReservedBytes = size_t{64} * 1024 * 1024;
    inline constexpr size_t kDefaultCommitChunkBytes = size_t{1} * 1024 * 1024;

    /// Reserves `reserveBytes` of address space with no access and makes it readable and writable in steps of
    /// `commitChunkBytes` as allocations pass the committed end (rounded up to a multiple of 64 KiB, which a page size
    /// divides). Allocations are consecutive addresses: the arena is bump-only, because the pool never frees and a range
    /// that is never moved or copied keeps every entry pointer valid. A call that would pass `reserveBytes`, or a refused
    /// commit, gives an empty `PoolMemory`; the committed memory is then left as it was. The destructor releases the range.
    ///
    /// Returns null when the address space cannot be reserved (a small `ulimit -v`). Not available on wasm: it returns
    /// null there.
    [[nodiscard]] UniquePtr<IPoolBlockSource> MakeVirtualBlockSource(size_t reserveBytes = kDefaultReservedBytes, size_t commitChunkBytes = kDefaultCommitChunkBytes);

    /// The virtual source on native platforms, the heap source on wasm and when the reservation fails
    [[nodiscard]] UniquePtr<IPoolBlockSource> MakeDefaultBlockSource();

}  // namespace BECore
