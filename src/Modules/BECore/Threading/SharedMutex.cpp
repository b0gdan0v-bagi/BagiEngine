#include "SharedMutex.h"

#if BE_LOCK_SITES

namespace BECore {

    void SharedMutex::LockContended() {
        const uint64_t begin = LockHooks::WaitBegin();
        _native.lock();
        LockHooks::WaitEnd(*_site, begin);
    }

    void SharedMutex::LockContendedShared() {
        const uint64_t begin = LockHooks::WaitBegin();
        _native.lock_shared();
        LockHooks::WaitEnd(*_site, begin);
    }

}  // namespace BECore

#endif
