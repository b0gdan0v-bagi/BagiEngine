#include <BECore/Memory/Immortal.h>
#include <TaskSystem/TaskMemory.h>
#include <new>

namespace BECore::Detail {

    namespace {

        constexpr size_t kBlockAlignment = TaskMemory::kBlockAlignment;

        constexpr size_t kClassSizes[TaskMemory::kClassCount] = {64, 96, 128, 192, 256, 384, 512, 768, 1024};
        static_assert(kClassSizes[TaskMemory::kClassCount - 1] == TaskMemory::kMaxPooledSize);

        // The alignment is the target's (16 on desktop, 8 on wasm32), so what the pools need is the contract, not the number: a power of two that a
        // pointer fits in, and a block of every class that keeps the next block aligned too
        constexpr bool AllClassSizesAreMultiplesOf(size_t alignment) {
            for (const size_t size : kClassSizes) {
                if (size % alignment != 0) {
                    return false;
                }
            }
            return true;
        }
        static_assert((kBlockAlignment & (kBlockAlignment - 1)) == 0 && kBlockAlignment >= alignof(void*));
        static_assert(AllClassSizesAreMultiplesOf(kBlockAlignment));

        // Immortal: a task may be destroyed by a static destructor after the pools would have been
        constinit Immortal<ConcurrentBlockPool> gPools[TaskMemory::kClassCount] = {
            Immortal<ConcurrentBlockPool>("Task memory 64", 64, kBlockAlignment),     Immortal<ConcurrentBlockPool>("Task memory 96", 96, kBlockAlignment),
            Immortal<ConcurrentBlockPool>("Task memory 128", 128, kBlockAlignment),   Immortal<ConcurrentBlockPool>("Task memory 192", 192, kBlockAlignment),
            Immortal<ConcurrentBlockPool>("Task memory 256", 256, kBlockAlignment),   Immortal<ConcurrentBlockPool>("Task memory 384", 384, kBlockAlignment),
            Immortal<ConcurrentBlockPool>("Task memory 512", 512, kBlockAlignment),   Immortal<ConcurrentBlockPool>("Task memory 768", 768, kBlockAlignment),
            Immortal<ConcurrentBlockPool>("Task memory 1024", 1024, kBlockAlignment),
        };

    }  // namespace

    size_t TaskMemory::GetClassIndex(size_t size) {
        for (size_t i = 0; i < kClassCount; ++i) {
            if (size <= kClassSizes[i]) {
                return i;
            }
        }
        return kClassCount;
    }

    size_t TaskMemory::GetClassSize(size_t classIndex) {
        return kClassSizes[classIndex];
    }

    ConcurrentBlockPool& TaskMemory::GetPool(size_t classIndex) {
        return gPools[classIndex].value;
    }

    void* TaskMemory::Allocate(size_t size) {
        const size_t classIndex = GetClassIndex(size);
        if (classIndex == kClassCount) {
            return ::operator new(size);
        }
        return gPools[classIndex].value.Allocate();
    }

    void TaskMemory::Deallocate(void* ptr, size_t size) {
        const size_t classIndex = GetClassIndex(size);
        if (classIndex == kClassCount) {
            ::operator delete(ptr);
            return;
        }
        gPools[classIndex].value.Deallocate(ptr);
    }

}  // namespace BECore::Detail
