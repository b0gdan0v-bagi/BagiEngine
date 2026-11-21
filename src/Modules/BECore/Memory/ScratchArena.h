#pragma once

#include <BECore/Memory/LinearArena.h>

namespace BECore {

    /**
     * @brief The calling thread's scratch arena: temporary memory that lives inside one function call, taken and given back through ScratchScope.
     *
     * A `constinit thread_local LinearArena`, not a service: it works on any thread, before and without CoreManager, and allocates nothing
     * until the first Allocate. The thread's end frees its chunks and releases its ArenaStats slot. Memory of one thread's arena must not be
     * touched by another thread (the arena is not thread-safe).
     */
    class ScratchArena {
    public:
        static constexpr size_t kChunkSize = 64 * 1024;

        ScratchArena() = delete;

        [[nodiscard]] static LinearArena& Get();
    };

    /**
     * @brief Everything allocated through the scope (or from the scratch arena while it is alive) is given back when the scope ends.
     *
     * Scopes nest and end in reverse order, as local variables do. The memory must not outlive the scope, be returned to the caller or be
     * handed to another thread; the scope must end on the thread that made it (Debug asserts).
     */
    class ScratchScope {
    public:
        ScratchScope() : _arena(ScratchArena::Get()), _mark(_arena.GetMark()) {}
        ~ScratchScope();

        ScratchScope(const ScratchScope&) = delete;
        ScratchScope& operator=(const ScratchScope&) = delete;
        ScratchScope(ScratchScope&&) = delete;
        ScratchScope& operator=(ScratchScope&&) = delete;

        [[nodiscard]] LinearArena& GetArena() const {
            return _arena;
        }

        [[nodiscard]] void* Allocate(size_t size, size_t alignment = LinearArena::kDefaultAlignment) {
            return _arena.Allocate(size, alignment);
        }

        /// Storage for @p count objects of T, not constructed; no destructor runs when the scope ends (see LinearArena::Allocate)
        template <typename T>
        [[nodiscard]] T* Allocate(size_t count) {
            return _arena.Allocate<T>(count);
        }

    private:
        LinearArena& _arena;
        LinearArena::Mark _mark;
    };

}  // namespace BECore
