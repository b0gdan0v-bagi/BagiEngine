#include <Common/Printers.h>
#include <Events/EventBase.h>

namespace BECore::Tests {

    namespace TestEvents {
        struct SimpleEvent : public EventBase<SimpleEvent> {};

        struct DataEvent : public EventBase<DataEvent> {
            int value = 0;
            constexpr explicit DataEvent(int v) : value(v) {}
        };
    }  // namespace TestEvents

    namespace {

        class SimpleSubscriber : public SubscriptionHolder {
        public:
            // Subscribe() is protected in SubscriptionHolder; the test drives it directly.
            using SubscriptionHolder::Subscribe;

            int callCount = 0;
            int lastValue = 0;

            void OnSimpleEvent(const TestEvents::SimpleEvent&) {
                callCount++;
            }

            void OnDataEvent(const TestEvents::DataEvent& e) {
                callCount++;
                lastValue = e.value;
            }
        };

        class RaiiSubscriber : public SubscriptionHolder {
        public:
            int callCount = 0;

            void Initialize() {
                Subscribe<TestEvents::SimpleEvent, &RaiiSubscriber::OnEvent>(this);
            }

            void OnEvent(const TestEvents::SimpleEvent&) {
                callCount++;
            }
        };

    }  // namespace

    // Compile-time checks: a regression breaks the build.
    static_assert(std::is_default_constructible_v<TestEvents::SimpleEvent>);
    static_assert(std::is_constructible_v<TestEvents::DataEvent, int>);
    static_assert(std::is_default_constructible_v<SimpleSubscriber>);
    static_assert(std::is_default_constructible_v<RaiiSubscriber>);

    TEST(EventSubscriptionTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    TEST(EventSubscriptionTest, ADestroyedHolderIsUnsubscribed) {
        {
            RaiiSubscriber first;
            first.Initialize();
            RaiiSubscriber second;
            second.Initialize();

            TestEvents::SimpleEvent::Emit();
            TestEvents::SimpleEvent::Emit();
            EXPECT_EQ(first.callCount, 2);
            EXPECT_EQ(second.callCount, 2);
        }

        // Only the subscriber that is alive gets the event
        RaiiSubscriber later;
        later.Initialize();
        TestEvents::SimpleEvent::Emit();
        EXPECT_EQ(later.callCount, 1);
    }

    TEST(EventSubscriptionTest, UnsubscribeAll) {
        SimpleSubscriber subscriber;

        subscriber.Subscribe<TestEvents::SimpleEvent, &SimpleSubscriber::OnSimpleEvent>(&subscriber);
        subscriber.Subscribe<TestEvents::DataEvent, &SimpleSubscriber::OnDataEvent>(&subscriber);

        TestEvents::SimpleEvent::Emit();
        TestEvents::DataEvent::Emit(42);
        EXPECT_EQ(subscriber.callCount, 2) << "Both events should be received";
        EXPECT_EQ(subscriber.lastValue, 42) << "Data event value should be correct";

        TestEvents::SimpleEvent::UnsubscribeAll(&subscriber);

        TestEvents::SimpleEvent::Emit();
        EXPECT_EQ(subscriber.callCount, 2) << "SimpleEvent should not be received after UnsubscribeAll";

        TestEvents::DataEvent::Emit(100);
        EXPECT_EQ(subscriber.callCount, 3) << "DataEvent should still be received";
        EXPECT_EQ(subscriber.lastValue, 100) << "Data event value should be updated";

        TestEvents::DataEvent::UnsubscribeAll(&subscriber);
    }

}  // namespace BECore::Tests
