#include "MemTag.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>

namespace BECore {

#if BE_PROFILER_ALLOC

    MemTagScope::MemTagScope(MemTag tag) : _previous(AllocTracker::SwapTag(tag)) {}

    MemTagScope::~MemTagScope() {
        AllocTracker::SwapTag(_previous);
    }

#endif

}  // namespace BECore
