#pragma once

#include <EASTL/allocator.h>
#include <EASTL/internal/config.h>
#include <cstddef>

namespace BECore {

    /**
     * @brief The EASTL allocator of every engine container (`String`, `Vector`, `HashMap`, ... of Types/): the global `operator new[]` /
     * `operator delete[]`, so `AllocTracker` sees each block and counts it under the tag in force (Memory/CLAUDE.md, "Memory tags").
     *
     * - **It carries no tag:** the tag is the calling thread's scope, read where the block is recorded.
     * - **As large as `eastl::allocator` in every build:** it keeps the debug name only where `EASTL_NAME_ENABLED` (EASTL's own rule), so the
     *   shipped build holds an empty object and a container costs what it did on the EASTL allocator. The name is not reported anywhere.
     * - **All instances are equal,** so containers swap and move storage freely.
     *
     * `allocate` and `deallocate` are in EngineAllocator.cpp, as `eastl::allocator`'s are out of line, to keep the headers of Types/ light.
     * `offset` of the aligned form is ignored (EASTL passes 0).
     */
    class EngineAllocator {
    public:
        EngineAllocator(const char* name = EASTL_NAME_VAL(EASTL_ALLOCATOR_DEFAULT_NAME)) {
            SetName(name);
        }
        EngineAllocator(const EngineAllocator& other) = default;
        EngineAllocator(const EngineAllocator& /*other*/, const char* name) {
            SetName(name);
        }
        EngineAllocator& operator=(const EngineAllocator& other) = default;

        [[nodiscard]] void* allocate(size_t size, int flags = 0);
        [[nodiscard]] void* allocate(size_t size, size_t alignment, size_t offset, int flags = 0);
        void deallocate(void* ptr, size_t size);

        [[nodiscard]] const char* get_name() const {
#if EASTL_NAME_ENABLED
            return _name;
#else
            return EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
        }
        void set_name(const char* name) {
            SetName(name);
        }

        [[nodiscard]] friend bool operator==(const EngineAllocator&, const EngineAllocator&) {
            return true;
        }
        [[nodiscard]] friend bool operator!=(const EngineAllocator&, const EngineAllocator&) {
            return false;
        }

    private:
        void SetName([[maybe_unused]] const char* name) {
#if EASTL_NAME_ENABLED
            _name = name != nullptr ? name : EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
        }

#if EASTL_NAME_ENABLED
        const char* _name = EASTL_ALLOCATOR_DEFAULT_NAME;
#endif
    };

    static_assert(sizeof(EngineAllocator) == sizeof(eastl::allocator) && alignof(EngineAllocator) == alignof(eastl::allocator),
                  "a container on EngineAllocator must be as large as one on eastl::allocator in every build");

}  // namespace BECore
