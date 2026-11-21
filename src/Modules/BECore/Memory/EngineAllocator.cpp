#include <BECore/Memory/EngineAllocator.h>
#include <new>

#if BE_PROFILER_ALLOC && EASTL_NAME_ENABLED
#include <BECore/Profiler/Alloc/AllocTracker.h>
#endif

namespace BECore {

    namespace {

        // The name exists only where EASTL_NAME_ENABLED and the tracker runs only with the option; elsewhere this is empty. Set right before
        // the operator new, whose RecordAlloc takes it (Profiler/Alloc/CLAUDE.md, "Names")
        void NameNextBlock([[maybe_unused]] const EngineAllocator& allocator) {
#if BE_PROFILER_ALLOC && EASTL_NAME_ENABLED
            AllocTracker::SetNextName(allocator.get_name());
#endif
        }

    }  // namespace

    void* EngineAllocator::allocate(size_t size, int /*flags*/) {
        NameNextBlock(*this);
        return ::operator new[](size);
    }

    void* EngineAllocator::allocate(size_t size, size_t alignment, size_t /*offset*/, int /*flags*/) {
        NameNextBlock(*this);
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
