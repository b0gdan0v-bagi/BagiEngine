#include "AllocRouting.h"

#include <BECore/Profiler/Alloc/AllocPlatform.h>

namespace BECore {

#if BE_PROFILER_ALLOC

    void* AllocRouting::TrackedMalloc(AllocSource source, size_t size) {
        void* ptr = std::malloc(size);
        if (ptr != nullptr) {
            AllocTracker::OnRoutedAlloc(source, ptr, size, AllocPlatform::LibcUsableSize(ptr));
        }
        return ptr;
    }

    void* AllocRouting::TrackedCalloc(AllocSource source, size_t count, size_t size) {
        void* ptr = std::calloc(count, size);
        if (ptr != nullptr) {
            AllocTracker::OnRoutedAlloc(source, ptr, count * size, AllocPlatform::LibcUsableSize(ptr));
        }
        return ptr;
    }

    void* AllocRouting::TrackedRealloc(AllocSource source, void* ptr, size_t size) {
        if (ptr == nullptr) {
            return TrackedMalloc(source, size);
        }
        const size_t oldUsable = AllocPlatform::LibcUsableSize(ptr);
        void* result = std::realloc(ptr, size);
        if (result == nullptr) {
            // realloc(ptr, 0) frees the block on glibc and answers nullptr; a real failure keeps it
            if (size == 0) {
                AllocTracker::OnRoutedFree(source, ptr, oldUsable);
            }
            return nullptr;
        }
        // The old block is gone whether it moved or not: its address goes to the backend as a value and is never read
        AllocTracker::OnRoutedFree(source, ptr, oldUsable);
        AllocTracker::OnRoutedAlloc(source, result, size, AllocPlatform::LibcUsableSize(result));
        return result;
    }

    void AllocRouting::TrackedFree(AllocSource source, void* ptr) {
        if (ptr == nullptr) {
            return;
        }
        AllocTracker::OnRoutedFree(source, ptr, AllocPlatform::LibcUsableSize(ptr));
        std::free(ptr);
    }

#endif

}  // namespace BECore
