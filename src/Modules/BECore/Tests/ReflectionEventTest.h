#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/PoolString/PoolString.h>

namespace BECore::Tests {

    // =========================================================================
    // Test Event Types with BE_EVENT
    // =========================================================================

    namespace ReflectionTestEvents {

        /**
         * @brief Empty event for testing zero overhead
         */
        struct TestEmptyEvent {
            BE_EVENT(TestEmptyEvent)
        };

        /**
         * @brief Event with data fields for testing reflection
         */
        struct TestDataEvent {
            BE_EVENT(TestDataEvent)

            BE_REFLECT_FIELD int32_t value = 0;
            BE_REFLECT_FIELD float multiplier = 1.0f;

            constexpr TestDataEvent() = default;
            constexpr explicit TestDataEvent(int32_t v, float m = 1.0f) 
                : value(v), multiplier(m) {}
        };

        /**
         * @brief Event with string data for testing PoolString reflection
         */
        struct TestMessageEvent {
            BE_EVENT(TestMessageEvent)

            BE_REFLECT_FIELD PoolString message;
            BE_REFLECT_FIELD int32_t code = 0;

            TestMessageEvent() = default;
            explicit TestMessageEvent(PoolString msg, int32_t c = 0) 
                : message(std::move(msg)), code(c) {}
        };

    }  // namespace ReflectionTestEvents

    // =========================================================================
    // ReflectionEventTest
    // =========================================================================

    /**
     * @brief Test for BE_EVENT reflection system
     *
     * Tests:
     * - GetStaticTypeMeta() returns valid ClassMeta
     * - GetStaticTypeHash() is unique per event type
     * - ForEachFieldStatic iterates fields correctly
     * - sizeof(TestEmptyEvent) == 1 (no overhead)
     * - Field reflection works with various types
     */
    class ReflectionEventTest : public ITest {
        BE_CLASS(ReflectionEventTest)
    public:
        ReflectionEventTest() = default;
        ~ReflectionEventTest() override = default;

        bool Run() override;

    private:
        /**
         * @brief Compile-time tests using static_assert
         * @note Defined in .cpp after generated code is included
         */
        static constexpr void TestCompileTime();

        /**
         * @brief Test event ClassMeta functionality
         */
        bool TestEventMeta();

        /**
         * @brief Test event field reflection
         */
        bool TestEventFields();

        /**
         * @brief Test zero overhead for empty events
         */
        bool TestZeroOverhead();

        /**
         * @brief Test field iteration
         */
        bool TestFieldIteration();
    };

    // Compile-time validation of test class
    static_assert(ValidTest<ReflectionEventTest>);

}  // namespace BECore::Tests
