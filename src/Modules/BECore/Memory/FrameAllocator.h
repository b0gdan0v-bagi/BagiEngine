#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Memory/FrameArena.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /**
     * @brief The EASTL allocator over the calling thread's FrameArena: `FrameVector<T>` takes its memory from it.
     *
     * Same shape as ArenaAllocator: **deallocate does nothing** (a vector that grows by doubling leaves its old blocks until the frame ends:
     * `reserve` first), the container's destructor frees nothing, equality is the arena's. A default-constructed one binds to the calling
     * thread's arena, so the container belongs to that thread and to the frame it was made in.
     *
     * Debug stamps the frame and the thread in the constructor and asserts in `allocate` that both are still current: a container kept in
     * a member and grown in a later frame, or used on another thread, would write into memory that was given back. Release has no stamp.
     */
    class FrameAllocator {
    public:
        FrameAllocator() : _arena(&FrameArena::Get()) {
            Stamp();
        }
        /// EASTL names its allocators on construction (`allocator_type("EASTL vector")`); the name is not kept
        explicit FrameAllocator(const char* /*name*/) : FrameAllocator() {}

        [[nodiscard]] void* allocate(size_t size, int /*flags*/ = 0) {
            CheckStamp();
            return _arena->Allocate(size);
        }

        [[nodiscard]] void* allocate(size_t size, size_t alignment, size_t offset, int /*flags*/ = 0) {
            CheckStamp();
            ASSERT(offset == 0, "an arena allocation has no alignment offset");
            return _arena->Allocate(size, alignment);
        }

        void deallocate(void* /*ptr*/, size_t /*size*/) {}

        [[nodiscard]] const char* get_name() const {
            return "FrameAllocator";
        }
        void set_name(const char* /*name*/) {}

        [[nodiscard]] LinearArena& GetArena() const {
            return *_arena;
        }

        [[nodiscard]] friend bool operator==(const FrameAllocator& lhs, const FrameAllocator& rhs) {
            return lhs._arena == rhs._arena;
        }
        [[nodiscard]] friend bool operator!=(const FrameAllocator& lhs, const FrameAllocator& rhs) {
            return lhs._arena != rhs._arena;
        }

    private:
#if ENGINE_ENABLE_ASSERTS
        /// A value that differs per live thread: the address of one of its thread-locals
        static uintptr_t CurrentThreadToken() {
            thread_local const char marker = 0;
            return reinterpret_cast<uintptr_t>(&marker);
        }

        void Stamp() {
            _frame = FrameArena::GetFrameNumber();
            _thread = CurrentThreadToken();
        }

        void CheckStamp() const {
            ASSERT(_frame == FrameArena::GetFrameNumber() && _thread == CurrentThreadToken(),
                   "a frame container is used after its frame ended, or on another thread: keep it in the frame that made it");
        }

        uint64_t _frame = 0;
        uintptr_t _thread = 0;
#else
        void Stamp() {}
        void CheckStamp() const {}
#endif
        LinearArena* _arena;
    };

}  // namespace BECore
