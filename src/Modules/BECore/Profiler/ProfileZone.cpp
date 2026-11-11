#include "ProfileZone.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>

namespace BECore {

    ProfileZone::ProfileZone(const ProfileSite& site) noexcept : _site(site), _backend(Profiler::GetBackend()) {
#if BE_PROFILER_ALLOC
        const AllocTracker::ThreadCounters counters = AllocTracker::GetThreadCounters();
        _allocCountAtBegin = counters.count;
        _allocBytesAtBegin = counters.bytes;
#endif
        if (_backend != nullptr) {
            _token = _backend->BeginZone(site);
        }
    }

    ProfileZone::~ProfileZone() {
        ZoneAllocs allocs;
#if BE_PROFILER_ALLOC
        const AllocTracker::ThreadCounters counters = AllocTracker::GetThreadCounters();
        allocs.count = counters.count - _allocCountAtBegin;
        allocs.bytes = counters.bytes - _allocBytesAtBegin;
        AllocTracker::AddZone(_site, allocs.count, allocs.bytes);
#endif
        if (_backend != nullptr) {
            _backend->EndZone(_token, _site, allocs);
        }
    }

}  // namespace BECore
