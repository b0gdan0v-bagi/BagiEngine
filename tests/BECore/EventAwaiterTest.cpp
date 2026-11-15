#include <Common/Printers.h>
#include <Events/EventBase.h>
#include <TaskSystem/EventAwaiter.h>
#include <coroutine>

namespace BECore::Tests {

    namespace {
        struct AwaitedEvent : EventBase<AwaitedEvent> {
            int value = 0;

            explicit AwaitedEvent(int v) : value(v) {}
        };

        // A coroutine that starts at once and frees its own frame at the end: the test needs no scheduler.
        struct DetachedCoroutine {
            std::coroutine_handle<> handle;  // valid only while the coroutine is suspended

            struct promise_type {
                DetachedCoroutine get_return_object() noexcept {
                    return {std::coroutine_handle<promise_type>::from_promise(*this)};
                }
                std::suspend_never initial_suspend() noexcept {
                    return {};
                }
                std::suspend_never final_suspend() noexcept {
                    return {};
                }
                void return_void() noexcept {}
                void unhandled_exception() noexcept {}
            };
        };

        DetachedCoroutine AwaitTwice(Vector<int>& received, bool& finished) {
            const AwaitedEvent first = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(first.value);
            const AwaitedEvent second = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(second.value);
            finished = true;
        }

        DetachedCoroutine AwaitOnce(Vector<int>& received) {
            const AwaitedEvent event = co_await WaitForEvent<AwaitedEvent>();
            received.push_back(event.value);
        }

        class Bystander : public SubscriptionHolder {
        public:
            using SubscriptionHolder::Subscribe;

            void OnAwaited(const AwaitedEvent& e) {
                seen.push_back(e.value);
            }

            Vector<int> seen;
        };
    }  // namespace

    TEST(EventAwaiterTest, TheCoroutineResumesWithTheEventAndEachAwaitTakesOneEmit) {
        Vector<int> received;
        bool finished = false;
        Bystander bystander;
        bystander.Subscribe<AwaitedEvent, &Bystander::OnAwaited>(&bystander);

        AwaitTwice(received, finished);
        EXPECT_TRUE(received.empty()) << "nothing arrives before the event";

        AwaitedEvent::Emit(1);
        EXPECT_EQ(received, (Vector<int>{1}));
        EXPECT_FALSE(finished);

        AwaitedEvent::Emit(2);
        EXPECT_EQ(received, (Vector<int>{1, 2}));
        EXPECT_TRUE(finished);

        // The finished coroutine left nothing behind: an event now reaches only the bystander.
        AwaitedEvent::Emit(3);
        EXPECT_EQ(received, (Vector<int>{1, 2}));
        EXPECT_EQ(bystander.seen, (Vector<int>{1, 2, 3}));
    }

    TEST(EventAwaiterTest, DestroyingASuspendedCoroutineRemovesItsListener) {
        Vector<int> received;
        Bystander bystander;
        bystander.Subscribe<AwaitedEvent, &Bystander::OnAwaited>(&bystander);

        DetachedCoroutine coroutine = AwaitOnce(received);
        coroutine.handle.destroy();

        AwaitedEvent::Emit(5);

        EXPECT_TRUE(received.empty());
        EXPECT_EQ(bystander.seen, (Vector<int>{5}));
    }

}  // namespace BECore::Tests
