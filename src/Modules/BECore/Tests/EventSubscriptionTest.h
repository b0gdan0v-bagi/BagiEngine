#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <Events/EventBase.h>
#include <Events/SubscriptionHolder.h>

namespace BECore::Tests {

    class EventSubscriptionTest;

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
        friend class EventSubscriptionTest;
    public:
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

    // =========================================================================
    // EventSubscriptionTest
    // =========================================================================

    /**
     * @brief Test for EnTT event subscription/unsubscription
     *
     * Tests:
     * - Manual Subscribe/Unsubscribe
     * - scoped_connection RAII behavior
     * - SubscriptionHolder auto-cleanup
     * - Destructor order (subscriber destroyed before emitter)
     */
    class EventSubscriptionTest : public ITest {
        BE_CLASS(EventSubscriptionTest)
    public:
        EventSubscriptionTest() = default;
        ~EventSubscriptionTest() override = default;

        bool Run() override;
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        /**
         * @brief Compile-time tests using static_assert
         */
        static constexpr void TestCompileTime() {
            // Events are default constructible
            static_assert(std::is_default_constructible_v<TestEvents::SimpleEvent>);
            static_assert(std::is_constructible_v<TestEvents::DataEvent, int>);

            // Subscriber classes are properly constructible
            static_assert(std::is_default_constructible_v<SimpleSubscriber>);
            static_assert(std::is_default_constructible_v<RaiiSubscriber>);
        }

        /**
         * @brief Test SubscriptionHolder auto-cleanup
         */
        bool TestSubscriptionHolder();

        /**
         * @brief Test that unsubscribe happens in destructor
         */
        bool TestDestructorUnsubscribe();

        /**
         * @brief Test that UnsubscribeAll works correctly
         */
        bool TestUnsubscribeAll();
    };

    // Compile-time validation of test class
    static_assert(ValidTest<EventSubscriptionTest>);

}  // namespace BECore::Tests
