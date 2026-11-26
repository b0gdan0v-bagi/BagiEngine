#pragma once

#include <BECore/PoolString/PoolString.h>

namespace BECore {

    /// Hears the strings a pool copies, for a tool that watches it grow (`Details::PoolStringStorage::SetObserver`)
    class IPoolStringObserver {
    public:
        virtual ~IPoolStringObserver() = default;

        /// Once per string that `Intern` or `InternBatch` copied into the pool: not for a hit, a repeat inside a batch, a
        /// registered or an adopted entry. Runs on the interning thread under the unique lock of the pool, so it **must not
        /// call the pool** (the lock is not recursive) and holds every other miss up while it runs.
        virtual void OnRuntimeString(PoolString text) = 0;
    };

}  // namespace BECore
