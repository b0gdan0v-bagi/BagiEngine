#include "AllocPlatform.h"

#include <cstdlib>

#if defined(__APPLE__)
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

namespace BECore {

#if defined(_WIN32)

    namespace {

        /// Every block is _aligned_malloc memory, so _aligned_free is right for each whatever operator delete frees it
        constexpr size_t kMinimumAlignment = 16;

    }  // namespace

    void* AllocPlatform::Allocate(size_t size) {
        return _aligned_malloc(size == 0 ? 1 : size, kMinimumAlignment);
    }

    void* AllocPlatform::AllocateAligned(size_t size, size_t alignment) {
        return _aligned_malloc(size == 0 ? 1 : size, alignment < kMinimumAlignment ? kMinimumAlignment : alignment);
    }

    void AllocPlatform::Free(void* ptr) {
        _aligned_free(ptr);
    }

    size_t AllocPlatform::UsableSize(void* ptr) {
        // _aligned_msize needs the alignment, which is not kept. The CRT keeps the pointer of the underlying malloc block in the word
        // in front of an aligned block (what _aligned_free and _aligned_msize read); its _msize is the block with the padding
        void* const* header = static_cast<void* const*>(ptr) - 1;
        return _msize(*header);
    }

#else

    void* AllocPlatform::Allocate(size_t size) {
        return std::malloc(size == 0 ? 1 : size);
    }

    void* AllocPlatform::AllocateAligned(size_t size, size_t alignment) {
        void* ptr = nullptr;
        const size_t minimum = alignment < sizeof(void*) ? sizeof(void*) : alignment;
        return posix_memalign(&ptr, minimum, size == 0 ? 1 : size) == 0 ? ptr : nullptr;
    }

    void AllocPlatform::Free(void* ptr) {
        std::free(ptr);
    }

    size_t AllocPlatform::UsableSize(void* ptr) {
#if defined(__APPLE__)
        return malloc_size(ptr);
#else
        return malloc_usable_size(ptr);
#endif
    }

#endif

}  // namespace BECore
