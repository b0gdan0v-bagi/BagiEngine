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

        /// A list of two-field objects: the benchmark of the native sequence
        struct JsonWriteInnerList {
            BE_CLASS(JsonWriteInnerList)

            BE_REFLECT_FIELD Vector<JsonWriteInner> items;
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

        /// A chain of three: the keys of the levels interleave (a, b, c, m ...), `m` and `same` are in more than one level (the most derived wins)
        /// and the private fields of a parent are out of reach of its children
        struct JsonWriteChainBase {
            BE_CLASS(JsonWriteChainBase)

            BE_REFLECT_FIELD int32_t same = 1;
            BE_REFLECT_FIELD int32_t _m = 2;
            BE_REFLECT_FIELD int32_t a = 3;
            BE_REFLECT_FIELD int32_t g = 4;

            void SetHidden(int32_t value) {
                _hidden = value;
            }

        private:
            BE_REFLECT_FIELD int32_t _hidden = 5;
        };

        struct JsonWriteChainMid : JsonWriteChainBase {
            BE_CLASS(JsonWriteChainMid)

            BE_REFLECT_FIELD String same;
            BE_REFLECT_FIELD String e;
            BE_REFLECT_FIELD String b;
            BE_REFLECT_FIELD Optional<int32_t> maybe;
        };

        struct JsonWriteChainLeaf : JsonWriteChainMid {
            BE_CLASS(JsonWriteChainLeaf)

            BE_REFLECT_FIELD Vector<int32_t> m;
            BE_REFLECT_FIELD Vector<JsonWriteInner> z;
            BE_REFLECT_FIELD bool _0first = false;
            BE_REFLECT_FIELD int32_t c = 6;
        };

        /// No fields of its own between two levels that have some
        struct JsonWriteChainEmptyMid : JsonWriteChainBase {
            BE_CLASS(JsonWriteChainEmptyMid)
        };

        struct JsonWriteChainEmptyLeaf : JsonWriteChainEmptyMid {
            BE_CLASS(JsonWriteChainEmptyLeaf)

            BE_REFLECT_FIELD int32_t f = 7;
            BE_REFLECT_FIELD int32_t _a = 8;
        };

        /// A parent with no fields at all
        struct JsonWriteChildOfEmpty : JsonWriteEmpty {
            BE_CLASS(JsonWriteChildOfEmpty)

            BE_REFLECT_FIELD int32_t value2 = 9;
            BE_REFLECT_FIELD int32_t value1 = 10;
        };

        /// Every key of the child sorts before every key of the parent
        struct JsonWriteChildBefore : JsonWriteInner {
            BE_CLASS(JsonWriteChildBefore)

            BE_REFLECT_FIELD int32_t aaa = 11;
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
