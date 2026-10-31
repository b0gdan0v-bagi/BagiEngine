#include <Common/Printers.h>
#include <Events/EventBase.h>

namespace BECore::Tests {

    // =========================================================================
    // Test Events
    // =========================================================================

    namespace TestEvents {
        struct SimpleEvent : public EventBase<SimpleEvent> {};

        struct DataEvent : public EventBase<DataEvent> {
            int value = 0;
            constexpr explicit DataEvent(int v) : value(v) {}
        };
    }  // namespace TestEvents

    // =========================================================================
    // Test Subscriber Classes
    // =========================================================================

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

    // Compile-time checks: a regression breaks the build.
    static_assert(std::is_default_constructible_v<TestEvents::SimpleEvent>);
    static_assert(std::is_constructible_v<TestEvents::DataEvent, int>);
    static_assert(std::is_default_constructible_v<SimpleSubscriber>);
    static_assert(std::is_default_constructible_v<RaiiSubscriber>);

    TEST(EventSubscriptionTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts above are the check";
    }

    /// SubscriptionHolder unsubscribes automatically when it goes out of scope.
    TEST(EventSubscriptionTest, SubscriptionHolder) {
        {
            RaiiSubscriber subscriber;
            subscriber.Initialize();

            TestEvents::SimpleEvent::Emit();
            EXPECT_EQ(subscriber.callCount, 1) << "Event should be received";

            TestEvents::SimpleEvent::Emit();
            EXPECT_EQ(subscriber.callCount, 2) << "Event should be received twice";
        }

        // A new subscriber must be the only one that gets the event
        RaiiSubscriber newSubscriber;
        newSubscriber.Initialize();

        TestEvents::SimpleEvent::Emit();
        EXPECT_EQ(newSubscriber.callCount, 1) << "Only the new subscriber should receive the event";
    }

    /// Unsubscribing happens in the destructor.
    TEST(EventSubscriptionTest, DestructorUnsubscribe) {
        {
            RaiiSubscriber sub1;
            sub1.Initialize();

            RaiiSubscriber sub2;
            sub2.Initialize();

            TestEvents::SimpleEvent::Emit();

            EXPECT_EQ(sub1.callCount, 1) << "Sub1 should receive the event";
            EXPECT_EQ(sub2.callCount, 1) << "Sub2 should receive the event";
        }

        RaiiSubscriber sub3;
        sub3.Initialize();

        TestEvents::SimpleEvent::Emit();
        EXPECT_EQ(sub3.callCount, 1) << "Only sub3 should receive the event";
    }

    /// UnsubscribeAll removes one event type and leaves the others alone.
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
