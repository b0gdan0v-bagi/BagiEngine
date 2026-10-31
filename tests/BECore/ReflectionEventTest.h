#pragma once

#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/TypeTraits.h>

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
            constexpr explicit TestDataEvent(int32_t v, float m = 1.0f) : value(v), multiplier(m) {}
        };

        /**
         * @brief Event with string data for testing PoolString reflection
         */
        struct TestMessageEvent {
            BE_EVENT(TestMessageEvent)

            BE_REFLECT_FIELD PoolString message;
            BE_REFLECT_FIELD int32_t code = 0;

            TestMessageEvent() = default;
            explicit TestMessageEvent(PoolString msg, int32_t c = 0) : message(std::move(msg)), code(c) {}
        };

    }  // namespace ReflectionTestEvents

}  // namespace BECore::Tests
