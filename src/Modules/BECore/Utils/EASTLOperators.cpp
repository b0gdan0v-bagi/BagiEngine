// The operator new overloads EASTL calls when EASTL_DEBUGPARAMS_LEVEL >= 1; they drop the debug parameters and forward to the plain operators.

#include <cstddef>
#include <new>

// EASTL frees what they return with the plain operator delete[] even when it was allocated over-aligned. That is harmless with
// the C++ runtime's operators on macOS and Linux (posix_memalign memory goes to free()) and with the profiler's replacement
// (BECore/Profiler/Alloc/AllocPlatform.h), but not with the MSVC runtime, whose aligned blocks need the aligned delete.

void* operator new[](std::size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    return ::operator new[](size);
}

// EASTL passes the alignment in the third parameter and always 0 as the offset, so the offset is ignored.
void* operator new[](std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new[](size, std::align_val_t(alignment));
    }
    return ::operator new[](size);
}

void* operator new(std::size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    return ::operator new(size);
}

void* operator new(std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t(alignment));
    }
    return ::operator new(size);
}
