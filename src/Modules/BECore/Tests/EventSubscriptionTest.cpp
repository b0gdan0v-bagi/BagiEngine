#include <BECore/Assert/AssertMacros.h>
#include <BECore/Tests/EventSubscriptionTest.h>

#include <Generated/EventSubscriptionTest.gen.hpp>

namespace BECore::Tests {

    bool EventSubscriptionTest::Run() {
        TestCompileTime();

        ASSERT(TestSubscriptionHolder(), "TestSubscriptionHolder failed");
        ASSERT(TestDestructorUnsubscribe(), "TestDestructorUnsubscribe failed");
        ASSERT(TestUnsubscribeAll(), "TestUnsubscribeAll failed");

        return true;
    }

    bool EventSubscriptionTest::TestSubscriptionHolder() {
        {
            RaiiSubscriber subscriber;
            subscriber.Initialize();

            // Emit event
            TestEvents::SimpleEvent::Emit();
            ASSERT(subscriber.callCount == 1, "Event should be received");

            TestEvents::SimpleEvent::Emit();
            ASSERT(subscriber.callCount == 2, "Event should be received twice");

            // subscriber goes out of scope and auto-unsubscribes
        }

        // Create new subscriber to verify previous one is disconnected
        RaiiSubscriber newSubscriber;
        newSubscriber.Initialize();

        TestEvents::SimpleEvent::Emit();
        ASSERT(newSubscriber.callCount == 1, "Only new subscriber should receive event");

        return true;
    }

    bool EventSubscriptionTest::TestDestructorUnsubscribe() {
        int emitCount = 0;

        {
            RaiiSubscriber sub1;
            sub1.Initialize();

            RaiiSubscriber sub2;
            sub2.Initialize();

            TestEvents::SimpleEvent::Emit();
            emitCount++;

            ASSERT(sub1.callCount == 1, "Sub1 should receive event");
            ASSERT(sub2.callCount == 1, "Sub2 should receive event");

            // sub1 and sub2 destroyed here
        }

        // Create new subscriber to test that previous ones are gone
        RaiiSubscriber sub3;
        sub3.Initialize();

        TestEvents::SimpleEvent::Emit();
        ASSERT(sub3.callCount == 1, "Only sub3 should receive event");

        return true;
    }

    bool EventSubscriptionTest::TestUnsubscribeAll() {
        SimpleSubscriber subscriber;

        // Subscribe to multiple events
        subscriber.Subscribe<TestEvents::SimpleEvent, &SimpleSubscriber::OnSimpleEvent>(&subscriber);
        subscriber.Subscribe<TestEvents::DataEvent, &SimpleSubscriber::OnDataEvent>(&subscriber);

        // Emit both events
        TestEvents::SimpleEvent::Emit();
        TestEvents::DataEvent::Emit(42);
        ASSERT(subscriber.callCount == 2, "Both events should be received");
        ASSERT(subscriber.lastValue == 42, "Data event value should be correct");

        // Unsubscribe all from SimpleEvent
        TestEvents::SimpleEvent::UnsubscribeAll(&subscriber);

        // Emit SimpleEvent - should not be received
        TestEvents::SimpleEvent::Emit();
        ASSERT(subscriber.callCount == 2, "SimpleEvent should not be received after UnsubscribeAll");

        // Emit DataEvent - should still be received
        TestEvents::DataEvent::Emit(100);
        ASSERT(subscriber.callCount == 3, "DataEvent should still be received");
        ASSERT(subscriber.lastValue == 100, "Data event value should be updated");

        // Clean up
        TestEvents::DataEvent::UnsubscribeAll(&subscriber);

        return true;
    }

}  // namespace BECore::Tests
