#include "Mutex.h"

#if BE_LOCK_SITES

namespace BECore {

    void Mutex::LockContended() {
        const uint64_t begin = LockHooks::WaitBegin();
        _native.lock();
        LockHooks::WaitEnd(*_site, begin);
    }

}  // namespace BECore

#endif
