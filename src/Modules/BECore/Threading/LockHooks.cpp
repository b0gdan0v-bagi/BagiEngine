#include "LockHooks.h"

#if BE_LOCK_SITES

namespace BECore::LockHooks {

    uint64_t WaitBegin() noexcept {
        return 0;
    }

    void WaitEnd(const LockSite& site, uint64_t beginTicks) noexcept {
        static_cast<void>(site);
        static_cast<void>(beginTicks);
    }

}  // namespace BECore::LockHooks

#endif
