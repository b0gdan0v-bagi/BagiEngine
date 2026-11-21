#pragma once

#include <BECore/Math/Vec2.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    // =========================================================================
    // Reflected test data of the generated writer (WriteJson). The .gen.hpp is included in JsonWriteReflectTest.cpp only.
    // Fields are declared out of alphabetical order on purpose: the writer sorts the keys like JsonSerializer does.
    // =========================================================================

    namespace JsonWriteTestData {

        CORE_ENUM(JsonWriteMode, uint8_t, Off, On, Automatic)

        /// Two fields: an element of this class stays an object in a list
        struct JsonWriteInner {
            BE_CLASS(JsonWriteInner)

            BE_REFLECT_FIELD String name;
            BE_REFLECT_FIELD int32_t count = 0;
        };

        struct JsonWriteEmpty {
            BE_CLASS(JsonWriteEmpty)
        };

        /// Every scalar and string shape; the leading underscores are not part of a key
        struct JsonWriteScalars {
            BE_CLASS(JsonWriteScalars)

            BE_REFLECT_FIELD uint64_t _unsigned64 = 0;
            BE_REFLECT_FIELD int8_t tiny = 0;
            BE_REFLECT_FIELD uint8_t byte = 0;
            BE_REFLECT_FIELD int16_t shortValue = 0;
            BE_REFLECT_FIELD uint16_t unsignedShort = 0;
            BE_REFLECT_FIELD int32_t count = 0;
            BE_REFLECT_FIELD uint32_t unsigned32 = 0;
            BE_REFLECT_FIELD int64_t signed64 = 0;
            BE_REFLECT_FIELD bool active = false;
            BE_REFLECT_FIELD float ratio = 0.0f;
            BE_REFLECT_FIELD double scale = 0.0;
            BE_REFLECT_FIELD String name;
            BE_REFLECT_FIELD FixedString<24> fixedName;
            BE_REFLECT_FIELD PoolString pool;
            BE_REFLECT_FIELD JsonWriteMode mode = JsonWriteMode::Off;
        };

        /// Nested objects, optionals and sequences: every shape the writer handles without the DOM
        struct JsonWriteNested {
            BE_CLASS(JsonWriteNested)

            BE_REFLECT_FIELD Vector<JsonWriteInner> objects;
            BE_REFLECT_FIELD JsonWriteInner inner;
            BE_REFLECT_FIELD JsonWriteEmpty empty;
            BE_REFLECT_FIELD Optional<int32_t> maybeCount;
            BE_REFLECT_FIELD Optional<JsonWriteInner> maybeInner;
            BE_REFLECT_FIELD Optional<String> maybeText;
            BE_REFLECT_FIELD Vector<int32_t> numbers;
            BE_REFLECT_FIELD Vector<double> reals;
            BE_REFLECT_FIELD Vector<String> words;
            BE_REFLECT_FIELD FixedVector<int32_t, 4> fixedNumbers;
            BE_REFLECT_FIELD FixedVector<JsonWriteInner, 3> fixedObjects;
            BE_REFLECT_FIELD Array<int32_t, 3> triple{};
            BE_REFLECT_FIELD Vector<int32_t> none;
        };

        /// A one-member element of a list collapses to its scalar on the DOM path; the writer bridges the field
        struct JsonWriteOneValue {
            BE_CLASS(JsonWriteOneValue)

            BE_REFLECT_FIELD int32_t value = 0;
        };

        struct JsonWriteOneValueList {
            BE_CLASS(JsonWriteOneValueList)

            BE_REFLECT_FIELD Vector<JsonWriteOneValue> ones;
            BE_REFLECT_FIELD JsonWriteOneValue single;
        };

        /// Field types the writer takes through the DOM bridge
        struct JsonWriteBridged {
            BE_CLASS(JsonWriteBridged)

            BE_REFLECT_FIELD Variant<std::monostate, int32_t, String> choice;
            BE_REFLECT_FIELD HashMap<String, int32_t> table;
            BE_REFLECT_FIELD Pair<int32_t, String> pair;
            BE_REFLECT_FIELD Vector<Vec2> points;
            BE_REFLECT_FIELD Optional<Vec2> maybePoint;
        };

        /// A class with a reflected parent: the keys of both interleave, the whole object goes through the bridge
        struct JsonWriteChild : JsonWriteScalars {
            BE_CLASS(JsonWriteChild)

            BE_REFLECT_FIELD String label;
            BE_REFLECT_FIELD int32_t extra = 0;
        };

        struct JsonWriteEvent {
            BE_EVENT(JsonWriteEvent)

            BE_REFLECT_FIELD String text;
            BE_REFLECT_FIELD int32_t id = 0;
            BE_REFLECT_FIELD float weight = 0.0f;
            BE_REFLECT_FIELD Optional<int32_t> optionalId;
        };

    }  // namespace JsonWriteTestData

}  // namespace BECore
