// EASTLOperators.cpp
// Implementation of EASTL custom operator new/delete functions with debug parameters
// These are required when EASTL_DEBUGPARAMS_LEVEL >= 1

#include <new>
#include <cstddef>

// EASTL allocator uses these custom operator new[] functions when debug parameters are enabled
// These operators forward to the standard operators while accepting debug parameters

// Array new operator with debug parameters
void* operator new[](std::size_t size, const char* /*pName*/, int /*flags*/, 
                     unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/)
{
    return ::operator new[](size);
}

// Array new operator with alignment and debug parameters
// ВАЖНО: EASTL передаёт alignment в третьем параметре, offset обычно 0
void* operator new[](std::size_t size, std::size_t alignment, std::size_t /*offset*/, 
                     const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, 
                     const char* /*pFile*/, int /*line*/)
{
    // Игнорируем offset - EASTL всегда передаёт 0
    // Используем стандартный aligned new
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        return ::operator new[](size, std::align_val_t(alignment));
    }
    return ::operator new[](size);
}

// Non-array versions for single objects
void* operator new(std::size_t size, const char* /*pName*/, int /*flags*/, 
                   unsigned int /*debugFlags*/, const char* /*pFile*/, int /*line*/)
{
    return ::operator new(size);
}

void* operator new(std::size_t size, std::size_t alignment, std::size_t /*offset*/, 
                   const char* /*pName*/, int /*flags*/, unsigned int /*debugFlags*/, 
                   const char* /*pFile*/, int /*line*/)
{
    if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__)
    {
        return ::operator new(size, std::align_val_t(alignment));
    }
    return ::operator new(size);
}
