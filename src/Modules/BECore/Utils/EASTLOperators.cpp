// The operator new overloads EASTL calls when EASTL_DEBUGPARAMS_LEVEL >= 1; they forward to the plain operators. The debug
// parameters are dropped except the allocation name: with ENGINE_PROFILER_ALLOC it goes to AllocTracker (the profile build defines
// EASTL_NAME_ENABLED, cmake/BagiPlatformOptions.cmake); without it the overloads are plain forwarders.

#include <cstddef>
#include <new>

#if BE_PROFILER_ALLOC
#include <BECore/Profiler/Alloc/AllocTracker.h>
#endif

namespace {

    /// The allocation that follows on this thread is made under @p name
    void NameNextAllocation(const char* name) {
#if BE_PROFILER_ALLOC
        if (name != nullptr) {
            BECore::AllocTracker::SetNextName(name);
        }
#else
        static_cast<void>(name);
#endif
    }

}  // namespace

// EASTL frees what they return with the plain operator delete[] even when it was allocated over-aligned. That is harmless with
// the C++ runtime's operators on macOS and Linux (posix_memalign memory goes to free()) and with the replacement of the global
// operators (BECore/Profiler/Alloc/GlobalOperatorNew.cpp, in every Windows build), which frees every block through one path; it
// would not be with the MSVC runtime's own operators, whose aligned blocks need the aligned delete.

void* operator new[](std::size_t size, const char* pName, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    NameNextAllocation(pName);
    return ::operator new[](size);
}

// EASTL passes the alignment in the third parameter and always 0 as the offset, so the offset is ignored.
void* operator new[](std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* pName, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    NameNextAllocation(pName);
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new[](size, std::align_val_t(alignment));
    }
    return ::operator new[](size);
}

void* operator new(std::size_t size, const char* pName, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    NameNextAllocation(pName);
    return ::operator new(size);
}

void* operator new(std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* pName, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    NameNextAllocation(pName);
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t(alignment));
    }
    return ::operator new(size);
}
