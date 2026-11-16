#include <BECore/Assert/AssertEvent.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Threading/Thread.h>
#include <Common/AssertBridge.h>
#include <Common/EngineEnvironment.h>
#include <Events/SubscriptionHolder.h>
#include <atomic>
#include <thread>

namespace BECore::Tests {

    namespace {

        constexpr size_t kThreads = 4;
        constexpr int kPerThread = 200;
        constexpr int kLineStride = 1000;
        constexpr int kSubscribeRounds = 200;
        constexpr int kMaxEmitsWhileSubscribing = 3000;

        /// One expression per emitting thread: an entry is whole when its expression is the one of the thread its line names
        constexpr Array<const char*, kThreads> kExpressions = {"thread 0 expression", "thread 1 expression", "thread 2 expression", "thread 3 expression"};

        bool IsWhole(const AssertEvent& event) {
            const size_t thread = static_cast<size_t>(event.line / kLineStride);
            return thread < kThreads && event.expression == kExpressions[thread] && event.message == nullptr && event.type == AssertType::Expect;
        }

        void EmitAs(size_t thread, int index) {
            AssertEvent::Emit(__FILE__, static_cast<int>(thread) * kLineStride + index, kExpressions[thread], nullptr, AssertType::Expect);
        }

        /// Checks what reaches it: whole entries, one at a time. Its counters are plain because the assert lock is what makes them safe.
        class WholeEntryHandler : public SubscriptionHolder {
        public:
            void Connect() {
                Subscribe<AssertEvent, &WholeEntryHandler::OnAssert>(this);
            }

            /// Takes the assert lock for the disconnect, then drops the connection that the lock-free destructor path would have removed
            void Disconnect() {
                AssertEvent::UnsubscribeAll(this);
                UnsubscribeAll();
            }

            void OnAssert(const AssertEvent& event) {
                ++inside;
                maxInside = inside > maxInside ? inside : maxInside;
                torn += IsWhole(event) ? 0 : 1;
                --inside;
            }

            int inside = 0;
            int maxInside = 0;
            int torn = 0;
        };

        /// Fails an engine assertion of its own from inside OnAssert, once per outer assert
        class NestingHandler : public SubscriptionHolder {
        public:
            void Connect() {
                Subscribe<AssertEvent, &NestingHandler::OnAssert>(this);
            }

            void Disconnect() {
                AssertEvent::UnsubscribeAll(this);
                UnsubscribeAll();
            }

            void OnAssert(const AssertEvent&) {
                ++calls;
                if (depth == 0) {
                    ++depth;
                    AssertEvent::Emit(__FILE__, __LINE__, "nested expression", nullptr, AssertType::Expect);
                    --depth;
                }
            }

            int depth = 0;
            int calls = 0;
        };

    }  // namespace

    /// AssertEvent takes a lock of its own: any thread may emit it, and handlers run one at a time on the emitting thread
    class AssertThreadTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            if constexpr (!Thread::kAvailable) {
                GTEST_SKIP() << "no threads in this build";
            }
        }

        void TearDown() override {
            // The engine's own LogHandler pushed a line per assert from the workers; the frame flush would take them
            LogEvent::Flush();
        }
    };

    TEST_F(AssertThreadTest, ThreadsEmitWholeEntriesOneAtATime) {
        Vector<int> lines;
        int inside = 0;
        int maxInside = 0;
        int torn = 0;
        {
            AssertBridge::Capture capture([&](const AssertEvent& event) {
                ++inside;
                maxInside = inside > maxInside ? inside : maxInside;
                lines.push_back(event.line);
                torn += IsWhole(event) ? 0 : 1;
                --inside;
            });

            Vector<Thread> threads;
            for (size_t thread = 0; thread < kThreads; ++thread) {
                threads.emplace_back("AssertThreadTestEmitter", [thread] {
                    for (int i = 0; i < kPerThread; ++i) {
                        EmitAs(thread, i);
                    }
                });
            }
            for (Thread& thread : threads) {
                thread.join();
            }
        }

        EXPECT_EQ(lines.size(), kThreads * static_cast<size_t>(kPerThread));
        EXPECT_EQ(torn, 0);
        EXPECT_EQ(maxInside, 1);
    }

    TEST_F(AssertThreadTest, AHandlerIsConnectedAndDroppedWhileWorkersEmit) {
        AssertBridge::Capture capture;
        std::atomic<bool> stop{false};

        Vector<Thread> threads;
        for (size_t thread = 0; thread < kThreads; ++thread) {
            threads.emplace_back("AssertThreadTestEmitter", [thread, &stop] {
                for (int i = 0; i < kMaxEmitsWhileSubscribing && !stop.load(); ++i) {
                    EmitAs(thread, i % kLineStride);
                    std::this_thread::yield();
                }
            });
        }

        int torn = 0;
        int maxInside = 0;
        for (int round = 0; round < kSubscribeRounds; ++round) {
            WholeEntryHandler handler;
            handler.Connect();
            std::this_thread::yield();
            handler.Disconnect();
            torn += handler.torn;
            maxInside = handler.maxInside > maxInside ? handler.maxInside : maxInside;
        }
        stop.store(true);
        for (Thread& thread : threads) {
            thread.join();
        }

        EXPECT_EQ(torn, 0);
        EXPECT_LE(maxInside, 1);
    }

    TEST_F(AssertThreadTest, AHandlerThatFailsAnAssertItselfDoesNotDeadlock) {
        AssertBridge::Capture capture;
        NestingHandler handler;
        handler.Connect();

        Thread worker("AssertThreadTestEmitter", [] {
            for (int i = 0; i < kPerThread; ++i) {
                EmitAs(0, i);
            }
        });
        for (int i = 0; i < kPerThread; ++i) {
            EmitAs(1, i);
        }
        worker.join();
        handler.Disconnect();

        // Each outer assert reached it once and its own nested one once
        EXPECT_EQ(handler.calls, 4 * kPerThread);
    }

}  // namespace BECore::Tests
