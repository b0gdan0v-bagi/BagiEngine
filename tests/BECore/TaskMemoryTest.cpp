#include <BECore/GameManager/CoreManager.h>
#include <BECore/Memory/ConcurrentBlockPool.h>
#include <Common/EngineEnvironment.h>
#include <Common/Wait.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/TaskMemory.h>
#include <TaskSystem/TaskScheduler.h>
#include <TaskSystem/ThreadPool.h>
#include <coroutine>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using Detail::TaskMemory;

        constexpr size_t kTasks = 300;
        constexpr size_t kBlocks = 1000;
        constexpr size_t kBiggestPooled = TaskMemory::kMaxPooledSize;
        /// Larger than the last class: the frame of a coroutine that keeps this much across a suspension lives on the heap
        constexpr size_t kHeapFrameBytes = 4 * kBiggestPooled;

        /// Live blocks of every class together. Tasks other tests left parked stay in the count and a task made on another thread changes it, so only
        /// the difference between two readings of one test means something, and only while the test runs no pool or scheduler
        size_t LiveBlocks() {
            size_t total = 0;
            for (size_t i = 0; i < TaskMemory::kClassCount; ++i) {
                total += TaskMemory::GetPool(i).GetLiveCount();
            }
            return total;
        }

        class CountedGuard {
        public:
            explicit CountedGuard(std::atomic<int>& destroyed) : _destroyed(&destroyed) {}
            CountedGuard(CountedGuard&& other) noexcept : _destroyed(std::exchange(other._destroyed, nullptr)) {}
            CountedGuard(const CountedGuard&) = delete;
            CountedGuard& operator=(const CountedGuard&) = delete;
            CountedGuard& operator=(CountedGuard&&) = delete;
            ~CountedGuard() {
                if (_destroyed != nullptr) {
                    // Release: a case that has read the count may end and reuse the stack this thread has just written
                    _destroyed->fetch_add(1, std::memory_order_release);
                }
            }

        private:
            std::atomic<int>* _destroyed;
        };

        Task<int> Answer() {
            co_return 42;
        }

        Task<int> AwaitsAChild() {
            auto child = co_await Answer();
            co_return child.value_or(0) + 1;
        }

        Task<void> Parks() {
            co_await std::suspend_always{};  // nobody resumes it
        }

        Task<void> GuardedTask([[maybe_unused]] CountedGuard guard) {
            co_return;
        }

        /// Written before and after the suspension: the buffer's address escapes on both sides, so it is part of the frame at any optimisation level
        char* volatile gEscape = nullptr;

        Task<void> BigFrame() {
            char buffer[kHeapFrameBytes];
            gEscape = buffer;
            co_await std::suspend_always{};
            gEscape = buffer;
        }

    }  // namespace

    TEST(TaskMemoryTest, EveryRequestGetsTheSmallestClassThatFitsAndBiggerOnesGetNone) {
        for (size_t size = 1; size <= kBiggestPooled; ++size) {
            const size_t index = TaskMemory::GetClassIndex(size);
            ASSERT_LT(index, TaskMemory::kClassCount) << size;
            EXPECT_GE(TaskMemory::GetClassSize(index), size);
            if (index > 0) {
                EXPECT_LT(TaskMemory::GetClassSize(index - 1), size) << size;
            }
            EXPECT_EQ(TaskMemory::GetPool(index).GetBlockSize(), TaskMemory::GetClassSize(index));
        }
        EXPECT_EQ(TaskMemory::GetClassIndex(kBiggestPooled + 1), TaskMemory::kClassCount);
    }

    TEST(TaskMemoryTest, EveryBlockIsAlignedAsTheDefaultOperatorNewAligns) {
        EXPECT_EQ(TaskMemory::kBlockAlignment, static_cast<size_t>(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
        auto expectAligned = [](size_t size) {
            void* block = TaskMemory::Allocate(size);
            ASSERT_NE(block, nullptr);
            EXPECT_EQ(reinterpret_cast<uintptr_t>(block) % TaskMemory::kBlockAlignment, 0u) << size;
            TaskMemory::Deallocate(block, size);
        };
        for (size_t i = 0; i < TaskMemory::kClassCount; ++i) {
            expectAligned(TaskMemory::GetClassSize(i));
        }
        expectAligned(kBiggestPooled + 1);
    }

    TEST(TaskMemoryTest, TheClassesWorkAtTheAlignmentOfWasm32) {
        // wasm32 aligns the default `operator new` to 8; this is that configuration on a 64-bit host
        constexpr size_t kWasmAlignment = 8;
        // The counters registry keeps the pointer of the name, so each is a literal
        constexpr const char* kNames[TaskMemory::kClassCount] = {"Test wasm 64",  "Test wasm 96",  "Test wasm 128", "Test wasm 192", "Test wasm 256",
                                                                 "Test wasm 384", "Test wasm 512", "Test wasm 768", "Test wasm 1024"};
        for (size_t i = 0; i < TaskMemory::kClassCount; ++i) {
            const size_t classSize = TaskMemory::GetClassSize(i);
            ConcurrentBlockPool pool(kNames[i], classSize, kWasmAlignment);
            EXPECT_EQ(pool.GetBlockSize(), classSize);
            Vector<unsigned char*> blocks;
            while (pool.GetPageCount() < 2) {
                auto* block = static_cast<unsigned char*>(pool.Allocate());
                ASSERT_EQ(reinterpret_cast<uintptr_t>(block) % kWasmAlignment, 0u) << classSize;
                std::memset(block, static_cast<int>(blocks.size() % 251), classSize);
                blocks.push_back(block);
            }
            for (size_t b = 0; b < blocks.size(); ++b) {
                for (size_t byte = 0; byte < classSize; ++byte) {
                    ASSERT_EQ(blocks[b][byte], static_cast<unsigned char>(b % 251)) << "class " << classSize << " block " << b << " byte " << byte;
                }
            }
            for (unsigned char* block : blocks) {
                pool.Deallocate(block);
            }
            EXPECT_EQ(pool.GetLiveCount(), 0u);
        }
    }

    TEST(TaskMemoryTest, ABlockAboveTheLastClassIsAHeapBlockFreedWithTheSameSize) {
        const size_t before = LiveBlocks();
        void* block = TaskMemory::Allocate(kBiggestPooled + 1);
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(LiveBlocks(), before);
        std::memset(block, 0xAB, kBiggestPooled + 1);
        TaskMemory::Deallocate(block, kBiggestPooled + 1);
        EXPECT_EQ(LiveBlocks(), before);
    }

    TEST(TaskMemoryTest, AFreedBlockIsReusedAndThePagesDoNotGrow) {
        constexpr size_t kSize = 100;
        ConcurrentBlockPool& pool = TaskMemory::GetPool(TaskMemory::GetClassIndex(kSize));
        auto round = [] {
            Vector<void*> blocks;
            for (size_t i = 0; i < kBlocks; ++i) {
                blocks.push_back(TaskMemory::Allocate(kSize));
            }
            for (void* block : blocks) {
                TaskMemory::Deallocate(block, kSize);
            }
        };
        const size_t liveBefore = pool.GetLiveCount();
        round();
        const size_t pages = pool.GetPageCount();
        round();
        EXPECT_EQ(pool.GetPageCount(), pages);
        EXPECT_EQ(pool.GetLiveCount(), liveBefore);
    }

    TEST(TaskMemoryTest, ABlockFreedOnAnotherThreadFlowsBack) {
        constexpr size_t kSize = 200;
        ConcurrentBlockPool& pool = TaskMemory::GetPool(TaskMemory::GetClassIndex(kSize));
        const size_t liveBefore = pool.GetLiveCount();
        Vector<void*> blocks;
        for (size_t i = 0; i < kBlocks; ++i) {
            blocks.push_back(TaskMemory::Allocate(kSize));
        }
        ASSERT_EQ(pool.GetLiveCount(), liveBefore + kBlocks);

        std::thread freeing([&blocks] {
            for (void* block : blocks) {
                TaskMemory::Deallocate(block, kSize);
            }
        });
        freeing.join();
        ConcurrentBlockPool::DrainThreadCache();
        EXPECT_EQ(pool.GetLiveCount(), liveBefore);

        const size_t pages = pool.GetPageCount();
        for (size_t i = 0; i < kBlocks; ++i) {
            TaskMemory::Deallocate(TaskMemory::Allocate(kSize), kSize);
        }
        EXPECT_EQ(pool.GetPageCount(), pages) << "the blocks freed elsewhere were not handed out again";
    }

    TEST(TaskMemoryTest, AHandleAndItsFrameComeFromTheClassesAndGoBackWhenTheTasksFinish) {
        const size_t before = LiveBlocks();
        {
            Vector<IntrusivePtr<TaskHandle<int>>> handles;
            for (size_t i = 0; i < kTasks; ++i) {
                handles.push_back(New<TaskHandle<int>>(AwaitsAChild()));
            }
            EXPECT_GE(LiveBlocks() - before, 2 * kTasks) << "a handle and a frame each";
            for (auto& handle : handles) {
                handle->Start();
                ASSERT_TRUE(handle->IsDone());
                EXPECT_EQ(handle->GetResult().value_or(0), 43);
            }
        }
        EXPECT_EQ(LiveBlocks(), before);
    }

    TEST(TaskMemoryTest, TasksAbandonedWhileParkedAreGivenBackWithTheirHandles) {
        const size_t before = LiveBlocks();
        {
            Vector<IntrusivePtr<TaskHandle<void>>> handles;
            for (size_t i = 0; i < kTasks; ++i) {
                handles.push_back(New<TaskHandle<void>>(Parks()));
                handles.back()->Start();  // parks and keeps itself alive
            }
            handles.clear();
            EXPECT_GT(LiveBlocks(), before) << "a started task keeps itself alive";
            Detail::LiveTasks::GetInstance().AbandonAll();
        }
        EXPECT_EQ(LiveBlocks(), before);
    }

    TEST(TaskMemoryTest, AFrameIsDestroyedOnceWhenItsHandleDropsBeforeTheTaskRan) {
        std::atomic<int> destroyed{0};
        const size_t before = LiveBlocks();
        {
            auto handle = New<TaskHandle<void>>(GuardedTask(CountedGuard(destroyed)));
            EXPECT_EQ(destroyed.load(), 0);
        }
        EXPECT_EQ(destroyed.load(), 1);
        EXPECT_EQ(LiveBlocks(), before);
    }

    TEST(TaskMemoryTest, ATaskRunByTheManagerDestroysItsMoveOnlyParameterOnceAndGivesBackItsBlocks) {
        EngineEnvironment::EnsureCoreServices();
        std::atomic<int> destroyed{0};
        const size_t before = LiveBlocks();
        {
            auto handle = CoreManager::GetTaskManager().Run(GuardedTask(CountedGuard(destroyed)));
            handle->Wait();
            EXPECT_EQ(handle->GetStatus(), TaskStatus::Completed);
        }
        EXPECT_TRUE(WaitUntil([&] { return destroyed.load() == 1 && LiveBlocks() == before; })) << "destroyed " << destroyed.load();
        EXPECT_EQ(destroyed.load(), 1);
    }

    TEST(TaskMemoryTest, ATaskThatNeverRanIsDroppedByTheSchedulerShutdownWithItsFrameAndHandle) {
        std::atomic<int> destroyed{0};
        const size_t before = LiveBlocks();
        ThreadPool pool(1);
        TaskScheduler scheduler;
        scheduler.Initialize(&pool);

        auto handle = New<TaskHandle<void>>(GuardedTask(CountedGuard(destroyed)));
        scheduler.ScheduleDelayed([handle = std::move(handle)]() mutable { handle->Start(); }, std::chrono::hours(1), TaskPriority::Normal, ThreadType::Background);
        EXPECT_GT(LiveBlocks(), before);
        EXPECT_EQ(destroyed.load(), 0);

        scheduler.Shutdown();
        EXPECT_EQ(destroyed.load(), 1) << "the job held the last reference: the frame and its parameter go with it";
        pool.Shutdown();
        EXPECT_EQ(LiveBlocks(), before);
    }

    TEST(TaskMemoryTest, AFrameAboveTheLastClassWorksAndTakesNoBlockOfAClass) {
        const size_t before = LiveBlocks();
        {
            auto handle = New<TaskHandle<void>>(BigFrame());
            EXPECT_EQ(LiveBlocks() - before, 1u) << "the handle only: the frame is on the heap";
            handle->Start();
            EXPECT_FALSE(handle->IsDone());
            Detail::LiveTasks::GetInstance().AbandonAll();
        }
        EXPECT_EQ(LiveBlocks(), before);
    }

}  // namespace BECore::Tests
