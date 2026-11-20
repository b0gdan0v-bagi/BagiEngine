#include <BECore/Memory/EngineAllocator.h>
#include <new>

#if BE_PROFILER_ALLOC && EASTL_NAME_ENABLED
#include <BECore/Profiler/Alloc/AllocTracker.h>
#endif

namespace BECore {

    namespace {

        // The name is reported only where it exists (EASTL keeps it in a `_DEBUG` build) and the tracker runs; elsewhere this is empty
        void* Named([[maybe_unused]] const EngineAllocator& allocator, void* block, [[maybe_unused]] size_t size) {
#if BE_PROFILER_ALLOC && EASTL_NAME_ENABLED
            if (block != nullptr) {
                AllocTracker::OnNamedAlloc(allocator.get_name(), size);
            }
#endif
            return block;
        }

    }  // namespace

    void* EngineAllocator::allocate(size_t size, int /*flags*/) {
        return Named(*this, ::operator new[](size), size);
    }

    void* EngineAllocator::allocate(size_t size, size_t alignment, size_t /*offset*/, int /*flags*/) {
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
            return Named(*this, ::operator new[](size, std::align_val_t(alignment)), size);
        }
        return Named(*this, ::operator new[](size), size);
    }

    // The same plain delete[] for an over-aligned block as eastl::allocator uses (Utils/EASTLOperators.cpp explains why that is sound)
    void EngineAllocator::deallocate(void* ptr, size_t /*size*/) {
        ::operator delete[](ptr);
    }

}  // namespace BECore
