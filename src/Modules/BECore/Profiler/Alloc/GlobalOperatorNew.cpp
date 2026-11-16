// The replacement of every form of the global operator new / delete. It is compiled with ENGINE_PROFILER_ALLOC (AllocTracker counts
// the blocks) and on Windows in every build: the MSVC runtime's plain operator delete is free(), which does not take a block of its
// aligned operator new, and EASTL frees its over-aligned arrays that way. Here every block is AllocPlatform memory, so whichever
// delete frees it is right.
// Not in an AddressSanitizer build: the sanitizer's runtime defines these operators too (a duplicate symbol at link time).
// It reports EASTL's pair, an aligned new freed by a plain delete, as new-delete-type-mismatch (EastlAlignedAllocTest skips there).
// BECoreModule is linked WHOLE_ARCHIVE into every executable, so these definitions win over the C++ runtime's.
// Nothing here may log, assert or allocate: it runs before main, on every thread and inside the logger.

#if defined(__SANITIZE_ADDRESS__)
#define BE_GLOBAL_OPERATORS_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BE_GLOBAL_OPERATORS_UNDER_ASAN 1
#endif
#endif

#if BE_PROFILER_ALLOC || (defined(_WIN32) && !defined(BE_GLOBAL_OPERATORS_UNDER_ASAN))

#include <BECore/Profiler/Alloc/AllocPlatform.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <cstdlib>
#include <new>

namespace {

    using BECore::AllocPlatform;
    using BECore::AllocTracker;

    /// The throwing forms: without exceptions an allocation that the new handler cannot help ends the process
    void* AllocateOrDie(std::size_t size, std::size_t alignment) {
        for (;;) {
            void* ptr = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__ ? AllocPlatform::AllocateAligned(size, alignment) : AllocPlatform::Allocate(size);
            if (ptr != nullptr) {
                if constexpr (AllocTracker::kEnabled) {
                    AllocTracker::OnAlloc(ptr, size);
                }
                return ptr;
            }
            std::new_handler handler = std::get_new_handler();
            if (handler == nullptr) {
                std::abort();
            }
            handler();
        }
    }

    void* AllocateOrNull(std::size_t size, std::size_t alignment) noexcept {
        void* ptr = alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__ ? AllocPlatform::AllocateAligned(size, alignment) : AllocPlatform::Allocate(size);
        if constexpr (AllocTracker::kEnabled) {
            if (ptr != nullptr) {
                AllocTracker::OnAlloc(ptr, size);
            }
        }
        return ptr;
    }

    void Release(void* ptr) noexcept {
        if (ptr != nullptr) {
            if constexpr (AllocTracker::kEnabled) {
                AllocTracker::OnFree(ptr);
            }
            AllocPlatform::Free(ptr);
        }
    }

    constexpr std::size_t kPlain = 0;

}  // namespace

void* operator new(std::size_t size) {
    return AllocateOrDie(size, kPlain);
}
void* operator new[](std::size_t size) {
    return AllocateOrDie(size, kPlain);
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return AllocateOrNull(size, kPlain);
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return AllocateOrNull(size, kPlain);
}
void* operator new(std::size_t size, std::align_val_t alignment) {
    return AllocateOrDie(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return AllocateOrDie(size, static_cast<std::size_t>(alignment));
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return AllocateOrNull(size, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return AllocateOrNull(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* ptr) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr) noexcept {
    Release(ptr);
}
void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    Release(ptr);
}
void operator delete(void* ptr, std::size_t) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr, std::size_t) noexcept {
    Release(ptr);
}
void operator delete(void* ptr, std::align_val_t) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr, std::align_val_t) noexcept {
    Release(ptr);
}
void operator delete(void* ptr, std::size_t, std::align_val_t) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr, std::size_t, std::align_val_t) noexcept {
    Release(ptr);
}
void operator delete(void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    Release(ptr);
}
void operator delete[](void* ptr, std::align_val_t, const std::nothrow_t&) noexcept {
    Release(ptr);
}

#endif
