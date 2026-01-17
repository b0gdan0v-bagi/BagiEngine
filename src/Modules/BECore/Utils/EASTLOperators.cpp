// EASTLOperators.cpp
// Implementation of EASTL custom operator new[] functions with debug parameters
// These are required when EASTL_DEBUGPARAMS_LEVEL >= 1

#include <new>
#include <cstddef>

// EASTL allocator uses these custom operator new[] functions when debug parameters are enabled
// These operators forward to the standard operators while accepting debug parameters

// Array new operator with debug parameters
// Signature: void* operator new[](size_t size, const char* pName, int flags, unsigned int align, const char* pFile, int line)
void* operator new[](std::size_t size, const char* /*pName*/, int /*flags*/, unsigned int /*align*/, const char* /*pFile*/, int /*line*/)
{
    return ::operator new[](size);
}

// Array new operator with alignment and debug parameters
// Signature: void* operator new[](size_t size, size_t alignment, size_t offset, const char* pName, int flags, unsigned int align, const char* pFile, int line)
void* operator new[](std::size_t size, std::size_t alignment, std::size_t offset, const char* /*pName*/, int /*flags*/, unsigned int /*align*/, const char* /*pFile*/, int /*line*/)
{
    // For aligned allocation, we use the standard aligned new operator
    // Note: offset parameter is typically 0 for EASTL usage
    if (offset == 0 && alignment > 0)
    {
        return ::operator new[](size, std::align_val_t(alignment));
    }
    else if (alignment == 0)
    {
        // If alignment is 0, fall back to regular allocation
        return ::operator new[](size);
    }
    else
    {
        // If offset is non-zero, we need to allocate extra space and adjust
        // This is a simplified implementation - in practice, EASTL typically uses offset=0
        // Note: This may require corresponding delete operator with offset handling
        std::size_t totalSize = size + offset;
        void* p = ::operator new[](totalSize, std::align_val_t(alignment));
        return static_cast<char*>(p) + offset;
    }
}


