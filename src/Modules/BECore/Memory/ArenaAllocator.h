#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Memory/ScratchArena.h>
#include <cstddef>

namespace BECore {

    /**
     * @brief The EASTL allocator over a LinearArena: `ArenaVector<T>` and `ArenaString` (Types/) take their memory from an arena.
     *
     * - **deallocate does nothing.** A container that grows by doubling leaves its old blocks in the arena until Reset() or the scope's rewind,
     *   so the waste is up to the final size again: `reserve` first.
     * - **The container's destructor frees nothing**, so the arena may be reset with the container still alive, but then the container must not
     *   be used again. A container of non-trivial elements still runs their destructors itself.
     * - **A default-constructed one binds to the calling thread's ScratchArena**, as EASTL containers need a default constructor. Such a
     *   container belongs to the scope that is alive when it is made and must not cross threads or outlive that scope.
     * - **Equality is the arena's:** containers on the same arena may swap and move storage, the others copy.
     *
     * `offset` of the aligned form is ignored (EASTL passes 0).
     */
    class ArenaAllocator {
    public:
        /// The calling thread's ScratchArena
        ArenaAllocator() : _arena(&ScratchArena::Get()) {}
        /// EASTL names its allocators on construction (`allocator_type("EASTL vector")`); the name is not kept, the arena has its own
        explicit ArenaAllocator(const char* /*name*/) : ArenaAllocator() {}
        explicit ArenaAllocator(LinearArena& arena) : _arena(&arena) {}

        [[nodiscard]] void* allocate(size_t size, int /*flags*/ = 0) {
            return _arena->Allocate(size);
        }

        [[nodiscard]] void* allocate(size_t size, size_t alignment, size_t offset, int /*flags*/ = 0) {
            ASSERT(offset == 0, "an arena allocation has no alignment offset");
            return _arena->Allocate(size, alignment);
        }

        void deallocate(void* /*ptr*/, size_t /*size*/) {}

        [[nodiscard]] const char* get_name() const {
            return "ArenaAllocator";
        }
        void set_name(const char* /*name*/) {}

        [[nodiscard]] LinearArena& GetArena() const {
            return *_arena;
        }

        [[nodiscard]] friend bool operator==(const ArenaAllocator& lhs, const ArenaAllocator& rhs) {
            return lhs._arena == rhs._arena;
        }
        [[nodiscard]] friend bool operator!=(const ArenaAllocator& lhs, const ArenaAllocator& rhs) {
            return lhs._arena != rhs._arena;
        }

    private:
        LinearArena* _arena;
    };

}  // namespace BECore
