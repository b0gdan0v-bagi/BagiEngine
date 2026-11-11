// EASTLOperators.cpp
// Implementation of EASTL custom operator new/delete functions with debug parameters
// These are required when EASTL_DEBUGPARAMS_LEVEL >= 1

#include <cstddef>
#include <new>

// EASTL allocator uses these custom operator new[] functions when debug parameters are enabled
// These operators forward to the standard operators while accepting debug parameters.
// EASTL frees what they return with the plain operator delete[] even when it was allocated over-aligned. That is harmless with
// the C++ runtime's operators on macOS and Linux (posix_memalign memory goes to free()) and with the profiler's replacement
// (BECore/Profiler/Alloc/AllocPlatform.h), but not with the MSVC runtime, whose aligned blocks need the aligned delete.

// Array new operator with debug parameters
void* operator new[](std::size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    return ::operator new[](size);
}

// Array new operator with alignment and debug parameters
// ВАЖНО: EASTL передаёт alignment в третьем параметре, offset обычно 0
void* operator new[](std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    // Игнорируем offset - EASTL всегда передаёт 0
    // Используем стандартный aligned new
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new[](size, std::align_val_t(alignment));
    }
    return ::operator new[](size);
}

// Non-array versions for single objects
void* operator new(std::size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    return ::operator new(size);
}

void* operator new(std::size_t size, std::size_t alignment, std::size_t /*offset*/, const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/) {
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size, std::align_val_t(alignment));
    }
    return ::operator new(size);
}
