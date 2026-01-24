#pragma once

#include <BECore/Tests/ITest.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/ThreadPool.h>
#include <TaskSystem/TaskQueue.h>
#include <TaskSystem/CancellationToken.h>
#include <TaskSystem/TaskGroup.h>
#include <TaskSystem/TaskStatistics.h>

namespace BECore::Tests {

    /**
     * @brief Comprehensive tests for TaskSystem module
     */
    class TaskSystemTest : public ITest {
        BE_CLASS(TaskSystemTest)
    public:
        TaskSystemTest() = default;
        ~TaskSystemTest() override = default;

        bool Run() override;
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        // Test methods
        bool TestTaskBasic();
        bool TestTaskWithValue();
        bool TestTaskQueue();
        bool TestThreadPool();
        bool TestCancellationToken();
        bool TestTaskGroup();
        bool TestTaskStatistics();
    };

    // Compile-time validation of test class
    static_assert(ValidTest<TaskSystemTest>);

} // namespace BECore::Tests
