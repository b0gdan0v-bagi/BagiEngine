#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <cstdint>
#include <utility>

namespace BECore::Tests {

    // =========================================================================
    // Test Event Types with BE_EVENT
    // =========================================================================

    namespace ReflectionTestEvents {

        struct TestEmptyEvent {
            BE_EVENT(TestEmptyEvent)
        };

        struct TestDataEvent {
            BE_EVENT(TestDataEvent)

            BE_REFLECT_FIELD int32_t value = 0;
            BE_REFLECT_FIELD float multiplier = 1.0f;

            constexpr TestDataEvent() = default;
            constexpr explicit TestDataEvent(int32_t v, float m = 1.0f) : value(v), multiplier(m) {}
        };

        /// A PoolString field next to a plain one
        struct TestMessageEvent {
            BE_EVENT(TestMessageEvent)

            BE_REFLECT_FIELD PoolString message;
            BE_REFLECT_FIELD int32_t code = 0;

            TestMessageEvent() = default;
            explicit TestMessageEvent(PoolString msg, int32_t c = 0) : message(std::move(msg)), code(c) {}
        };

    }  // namespace ReflectionTestEvents

}  // namespace BECore::Tests
