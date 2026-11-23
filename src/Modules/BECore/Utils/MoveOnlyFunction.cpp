#include "MoveOnlyFunction.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>

namespace BECore::detail {

    void NameHeapCallable() noexcept {
        if constexpr (AllocTracker::kEnabled) {
            // AllocTracker keeps the pointer, not a copy: the name must be a literal (Profiler/Alloc/CLAUDE.md, "Names").
            AllocTracker::SetNextName("MoveOnlyFunction heap");
        }
    }

}  // namespace BECore::detail
