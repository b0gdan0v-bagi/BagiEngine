#include <BECore/Control/Json.h>
#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/IProfilerBackend.h>
#include <Common/EngineEnvironment.h>
#include <TaskSystem/Awaitables.h>
#include <TaskSystem/Task.h>
#include <TaskSystem/TaskHandle.h>
#include <TaskSystem/TaskLabel.h>
#include <TaskSystem/TaskManager.h>
#include <TaskSystem/ThreadPool.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

namespace BECore::Tests {

    namespace {

        /// Keeps what the profiler is told: the zones that began and the counters, with the value of each, in the order they came
        class RecordingBackend final : public IProfilerBackend {
        public:
            uint64_t BeginZone(const ProfileSite& site) override {
                std::lock_guard lock(_mutex);
                _zones.emplace_back(site.name);
                return 0;
            }
            void EndZone(uint64_t /*token*/, const ProfileSite& /*site*/, const ZoneAllocs& /*allocs*/) override {}
            void FrameMark() override {}
            void Counter(const char* name, double value) override {
                std::lock_guard lock(_mutex);
                _counters.emplace_back(name, value);
            }
            void SetThreadName(const char* /*name*/) override {}

            void Clear() {
                std::lock_guard lock(_mutex);
                _zones.clear();
                _counters.clear();
            }

            [[nodiscard]] bool HasZone(const std::string& name) const {
                std::lock_guard lock(_mutex);
                for (const std::string& zone : _zones) {
                    if (zone == name) {
                        return true;
                    }
                }
                return false;
            }

            [[nodiscard]] bool HasCounter(const std::string& name) const {
                return !Values(name).empty();
            }

            /// Every value the counter @p name took, in order
            [[nodiscard]] Vector<double> Values(const std::string& name) const {
                std::lock_guard lock(_mutex);
                Vector<double> values;
                for (const auto& [counter, value] : _counters) {
                    if (counter == name) {
                        values.push_back(value);
                    }
                }
                return values;
            }

        private:
            mutable std::mutex _mutex;
            Vector<std::string> _zones;
            Vector<Pair<std::string, double>> _counters;
        };

        /// The one backend of these tests, emptied. It is never destroyed: a worker of the TaskManager's pool may sit in a zone that began on
        /// it and ends on it later (the contract of IProfilerBackend)
        RecordingBackend& FreshBackend() {
            static RecordingBackend* const backend = new RecordingBackend();
            backend->Clear();
            return *backend;
        }

        /// Installs a backend for a scope. The pool of a test is shut down before the scope ends, so no zone is still open on it
        class ScopedBackend {
        public:
            explicit ScopedBackend(IProfilerBackend& backend) {
                Profiler::SetBackend(&backend);
            }
            ~ScopedBackend() {
                Profiler::SetBackend(nullptr);
            }
            ScopedBackend(const ScopedBackend&) = delete;
            ScopedBackend& operator=(const ScopedBackend&) = delete;
        };

        template <typename TCondition>
        bool WaitFor(TCondition condition) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (!condition()) {
                if (std::chrono::steady_clock::now() > deadline) {
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

        Task<void> NothingTask() {
            co_return;
        }

        Task<void> HoppingTask() {
            co_await SwitchToBackground(TaskPriority::Normal, BE_TASK_LABEL("Hop label"));
            co_return;
        }

        std::string Text(const String& text) {
            return std::string(text.data(), text.size());
        }

    }  // namespace

    /// The tests that go through the real TaskManager need the engine core services
    class TaskLabelTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    using TaskManagerProfileTest = TaskLabelTest;

#if BE_PROFILER

    TEST(ThreadPoolProfileTest, EveryWorkerHasABusyGraphAndThePoolSendsItsLoad) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);
        ThreadPool pool(2);

        std::atomic<bool> release{false};
        std::atomic<bool> running{false};
        pool.Submit([&] {
            running.store(true);
            while (!release.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
        ASSERT_TRUE(WaitFor([&] { return running.load(); }));

        EXPECT_EQ(pool.GetBusyWorkerCount(), 1u);
        // The busy graphs are reported by the workers on every change, the queues are sampled
        ASSERT_TRUE(backend.HasCounter("Pool: busy workers"));
        EXPECT_EQ(backend.Values("Pool: busy workers").back(), 1.0);
        pool.SampleProfilerCounters();
        EXPECT_EQ(backend.Values("Pool: pending jobs").back(), 0.0);
        EXPECT_TRUE(backend.HasCounter("Pool: steals"));

        // Both workers started, so both have a graph; exactly one of them is at 1 while the job runs
        ASSERT_TRUE(WaitFor([&] { return backend.HasCounter("Worker 0: busy") && backend.HasCounter("Worker 1: busy"); }));
        const double lastOfFirst = backend.Values("Worker 0: busy").back();
        const double lastOfSecond = backend.Values("Worker 1: busy").back();
        EXPECT_EQ(lastOfFirst + lastOfSecond, 1.0);

        release.store(true);
        ASSERT_TRUE(WaitFor([&] { return pool.GetBusyWorkerCount() == 0; }));
        pool.Shutdown();
        EXPECT_EQ(backend.Values("Pool: busy workers").back(), 0.0);

        // The graph of the worker that ran the job went 0 (start), 1 (the job), 0 (done)
        const Vector<double> values = backend.Values(lastOfFirst == 1.0 ? "Worker 0: busy" : "Worker 1: busy");
        ASSERT_GE(values.size(), 3u);
        EXPECT_EQ(values.front(), 0.0);
        EXPECT_EQ(values[values.size() - 2], 1.0);
        EXPECT_EQ(values.back(), 0.0);
    }

    TEST(ThreadPoolProfileTest, AWorkerThatWaitsRecordsAnIdleZone) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);
        ThreadPool pool(2);

        EXPECT_TRUE(WaitFor([&] { return backend.HasZone("Idle"); }));
        pool.Shutdown();
    }

    TEST(ThreadPoolProfileTest, AJobRunsInsideATaskZone) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);
        ThreadPool pool(2);

        std::atomic<bool> done{false};
        pool.Submit([&] { done.store(true); });
        ASSERT_TRUE(WaitFor([&] { return done.load(); }));
        pool.Shutdown();

        EXPECT_TRUE(backend.HasZone("Task"));
    }

    TEST(ThreadPoolProfileTest, WorkersAreNamedAndOrderedInTheChromeTrace) {
        // The process's recorder, which is never destroyed (workers of the TaskManager's pool may still end a zone on it)
        ChromeTraceBackend& recorder = Profiler::GetChromeTrace();
        recorder.MarkTraceStart();
        ScopedBackend installed(recorder);
        {
            ThreadPool pool(2);
            std::atomic<int> ran{0};
            pool.SubmitToThread(0, [&] { ran.fetch_add(1); });
            pool.SubmitToThread(1, [&] { ran.fetch_add(1); });
            ASSERT_TRUE(WaitFor([&] { return ran.load() == 2; }));
            pool.Shutdown();
        }

        auto parsed = ParseJson(recorder.WriteJson());
        ASSERT_TRUE(parsed.has_value());
        const Json* events = FindJsonMember(*parsed, "traceEvents");
        ASSERT_NE(events, nullptr);

        Vector<Pair<std::string, int64_t>> names;    // thread name, tid
        Vector<Pair<int64_t, int64_t>> sortIndexes;  // tid, sort index
        Vector<std::string> counters;
        for (const Json& event : *events) {
            const auto name = GetJsonString(event, "name");
            const auto phase = GetJsonString(event, "ph");
            if (!name || !phase) {
                continue;
            }
            if (*phase == "C") {
                counters.push_back(Text(*name));
            }
            const Json* args = FindJsonMember(event, "args");
            if (args == nullptr) {
                continue;
            }
            if (*name == "thread_name") {
                names.emplace_back(Text(*GetJsonString(*args, "name")), GetJsonInt(event, "tid").value_or(-1));
            } else if (*name == "thread_sort_index") {
                sortIndexes.emplace_back(GetJsonInt(event, "tid").value_or(-1), GetJsonInt(*args, "sort_index").value_or(-1));
            }
        }

        for (const auto& [workerName, expectedIndex] : {Pair<const char*, int64_t>{"Worker 0", 100}, {"Worker 1", 101}}) {
            int64_t tid = -1;
            for (const auto& [name, id] : names) {
                if (name == workerName) {
                    tid = id;
                }
            }
            ASSERT_GE(tid, 0) << workerName;
            int64_t sortIndex = -1;
            for (const auto& [id, index] : sortIndexes) {
                if (id == tid) {
                    sortIndex = index;
                }
            }
            EXPECT_EQ(sortIndex, expectedIndex) << workerName;
        }
        EXPECT_NE(std::find(counters.begin(), counters.end(), "Worker 0: busy"), counters.end());
        EXPECT_NE(std::find(counters.begin(), counters.end(), "Worker 1: busy"), counters.end());
    }

    TEST_F(TaskLabelTest, ARunLabelNamesTheFirstSliceOfATask) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);

        auto handle = TaskManager::GetInstance().Run(NothingTask(), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("Run label"));
        handle->Wait();

        EXPECT_TRUE(backend.HasZone("Run label"));
    }

    TEST_F(TaskLabelTest, ASwitchToBackgroundLabelNamesTheSliceAfterIt) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);

        auto handle = TaskManager::GetInstance().Run(HoppingTask());
        handle->Wait();

        EXPECT_TRUE(backend.HasZone("Hop label"));
    }

    TEST_F(TaskLabelTest, ATaskWithoutALabelAddsNoZone) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);

        auto handle = TaskManager::GetInstance().Run(NothingTask());
        handle->Wait();

        EXPECT_FALSE(backend.HasZone("Run label"));
        EXPECT_FALSE(backend.HasZone("Hop label"));
    }

    TEST_F(TaskManagerProfileTest, TheQueuesAreGraphs) {
        RecordingBackend& backend = FreshBackend();
        ScopedBackend installed(backend);

        TaskManager::GetInstance().SampleProfilerCounters();

        EXPECT_TRUE(backend.HasCounter("Main thread queue: pending"));
        EXPECT_TRUE(backend.HasCounter("Delayed tasks: pending"));
#if BE_SINGLE_THREAD
        EXPECT_TRUE(backend.HasCounter("Cooperative queue: pending"));
        EXPECT_FALSE(backend.HasCounter("Pool: pending jobs"));
#else
        EXPECT_TRUE(backend.HasCounter("Pool: pending jobs"));
        EXPECT_TRUE(backend.HasCounter("Pool: steals"));
        EXPECT_FALSE(backend.HasCounter("Cooperative queue: pending"));
#endif
    }

    TEST_F(TaskManagerProfileTest, NothingIsSampledWithoutABackend) {
        Profiler::SetBackend(nullptr);
        TaskManager::GetInstance().SampleProfilerCounters();  // must not crash and has nobody to tell
        SUCCEED();
    }

#else

    TEST_F(TaskLabelTest, WithoutTheProfilerALabelIsEmptyAndTheTaskStillRuns) {
        const TaskLabel label = BE_TASK_LABEL("Unused");
        EXPECT_EQ(label.site, nullptr);

        auto handle = TaskManager::GetInstance().Run(NothingTask(), TaskPriority::Normal, ThreadType::Background, label);
        handle->Wait();
        EXPECT_EQ(handle->GetStatus(), TaskStatus::Completed);
    }

    TEST(ThreadPoolProfileTest, WithoutTheProfilerTheBusyCountStillWorks) {
        ThreadPool pool(2);
        EXPECT_EQ(pool.GetBusyWorkerCount(), 0u);
        pool.SampleProfilerCounters();  // nothing to tell, nothing happens
        pool.Shutdown();
    }

#endif

}  // namespace BECore::Tests
