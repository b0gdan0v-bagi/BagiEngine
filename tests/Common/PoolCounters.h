#pragma once

#include <BECore/Memory/PoolStats.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    /// The `Allocate()` calls of the pool @p name so far, a running total; 0 while no pool of that name is registered. Pooled blocks are invisible to
    /// the allocation counters, so this is how a test sees that an object was made
    inline uint64_t PoolAllocations(StringView name) {
        for (const PoolStatsEntry& entry : GetPoolStats()) {
            if (StringView(entry.name) == name) {
                return entry.allocations;
            }
        }
        return 0;
    }

}  // namespace BECore::Tests
