#pragma once

#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore::Tests {

    /// A block source over the heap that counts what it is asked for and, with a limit, serves that many allocations and
    /// then answers every call with an empty `PoolMemory` (what a used-up reservation does). A test builds its own
    /// `PoolStringStorage` over one and compares absolute numbers.
    class CountingBlockSource final : public IPoolBlockSource {
        UniquePtr<IPoolBlockSource> _heap = MakeHeapBlockSource();
        size_t _limit;

    public:
        size_t served = 0;       ///< Allocations answered with memory
        size_t refused = 0;      ///< Allocations answered with an empty `PoolMemory`
        size_t servedBytes = 0;  ///< Their total size

        explicit CountingBlockSource(size_t limit = SIZE_MAX) : _limit(limit) {}

        PoolMemory Allocate(size_t bytes) override {
            if (served >= _limit) {
                ++refused;
                return {};
            }
            ++served;
            servedBytes += bytes;
            return _heap->Allocate(bytes);
        }
    };

}  // namespace BECore::Tests
