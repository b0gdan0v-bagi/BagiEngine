#include "NoAllocScope.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>

namespace BECore {

    NoAllocScope::NoAllocScope(const ProfileSite& site) : _site(site), _violationsAtBegin(AllocTracker::EnterNoAlloc(site)) {}

    NoAllocScope::~NoAllocScope() {
        // Left first: the warning below allocates
        const uint64_t violations = AllocTracker::LeaveNoAlloc() - _violationsAtBegin;
        if (violations > 0) {
            LOG_WARNING("Profiler: {} allocation(s) inside the no-alloc scope '{}' ({}:{})"_format(violations, _site.name, _site.file, _site.line));
        }
    }

}  // namespace BECore
