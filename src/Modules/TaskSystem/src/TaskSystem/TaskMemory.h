#pragma once

#include <BECore/Memory/ConcurrentBlockPool.h>
#include <cstddef>
#include <new>

namespace BECore::Detail {

    /**
     * TaskMemory - the memory of a task: its TaskHandle and its coroutine frame. Frames of different coroutines have different sizes and
     * the thread that ends a task frees them, so the blocks come from a ladder of size-class ConcurrentBlockPools instead of one pool per type.
     * A request above the last class goes to `::operator new`.
     *
     * **The size is the contract.** Deallocate() picks the class from the size it is given, which is what the sized `operator delete` of a
     * coroutine promise and of a class gets (a virtual destructor passes the dynamic size), so Allocate() and Deallocate() of one block must
     * be called with the same size. Pages stay in their pool until exit: the memory follows the peak of tasks alive at once.
     *
     * **The alignment** is that of the default `operator new` of the target (16 on desktop, 8 on wasm32): a frame and a handle have only the
     * size-only `operator new`, so the compiler promises them no more and a block is given no more. A type that needs more is not given more.
     */
    class TaskMemory {
    public:
        static constexpr size_t kBlockAlignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__;
        static constexpr size_t kClassCount = 9;
        static constexpr size_t kMaxPooledSize = 1024;

        [[nodiscard]] static void* Allocate(size_t size);
        static void Deallocate(void* ptr, size_t size);

        /// The class that serves @p size, or kClassCount when it is above the last one
        [[nodiscard]] static size_t GetClassIndex(size_t size);
        [[nodiscard]] static size_t GetClassSize(size_t classIndex);
        [[nodiscard]] static ConcurrentBlockPool& GetPool(size_t classIndex);
    };

}  // namespace BECore::Detail
