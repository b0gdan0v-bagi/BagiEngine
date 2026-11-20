#include <BECore/Memory/EngineAllocator.h>
#include <new>

namespace BECore {

    void* EngineAllocator::allocate(size_t size, int /*flags*/) {
        return ::operator new[](size);
    }

    void* EngineAllocator::allocate(size_t size, size_t alignment, size_t /*offset*/, int /*flags*/) {
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return ::operator new[](size, std::align_val_t(alignment));
        }
        return ::operator new[](size);
    }

    // The same plain delete[] for an over-aligned block as eastl::allocator uses (Utils/EASTLOperators.cpp explains why that is sound)
    void EngineAllocator::deallocate(void* ptr, size_t /*size*/) {
        ::operator delete[](ptr);
    }

}  // namespace BECore
