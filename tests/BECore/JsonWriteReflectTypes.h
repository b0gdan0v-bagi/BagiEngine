#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
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

        /// Pairs, variants and maps of scalars and of the other shapes
        struct JsonWriteBridged {
            BE_CLASS(JsonWriteBridged)

            BE_REFLECT_FIELD Variant<std::monostate, int32_t, String> choice;
            BE_REFLECT_FIELD HashMap<String, int32_t> table;
            BE_REFLECT_FIELD Pair<int32_t, String> pair;
            BE_REFLECT_FIELD Vector<Vec2> points;
            BE_REFLECT_FIELD Optional<Vec2> maybePoint;
        };

        /// Every Math type once, in a list and in an optional
        struct JsonWriteMathFields {
            BE_CLASS(JsonWriteMathFields)

            BE_REFLECT_FIELD Vec2 vec2;
            BE_REFLECT_FIELD Vec3 vec3;
            BE_REFLECT_FIELD Vec4 vec4;
            BE_REFLECT_FIELD IVec2 ivec2;
            BE_REFLECT_FIELD Quat quat;
            BE_REFLECT_FIELD Color color;
            BE_REFLECT_FIELD Rect rect;
            BE_REFLECT_FIELD Vector<Vec2> points;
            BE_REFLECT_FIELD Vector<Color> colors;
            BE_REFLECT_FIELD FixedVector<Rect, 2> rects;
            BE_REFLECT_FIELD Optional<Quat> maybeQuat;
            BE_REFLECT_FIELD Optional<Vec3> noVec3;
        };

        /// A member named `key` and one named `value` next to it: the map entry's own `key` is replaced by it
        struct JsonWriteKeyed {
            BE_CLASS(JsonWriteKeyed)

            BE_REFLECT_FIELD String key;
            BE_REFLECT_FIELD int32_t zed = 0;
        };

        /// Maps of every native shape: hash, fixed hash and ordered, scalar and object values, number and string keys
        struct JsonWriteMaps {
            BE_CLASS(JsonWriteMaps)

            BE_REFLECT_FIELD HashMap<String, JsonWriteInner> objects;
            BE_REFLECT_FIELD HashMap<String, int32_t> numbers;
            BE_REFLECT_FIELD HashMap<int32_t, String> names;
            BE_REFLECT_FIELD FixedHashMap<String, double, 8> fixedTable;
            BE_REFLECT_FIELD Map<String, JsonWriteInner> ordered;
            BE_REFLECT_FIELD Map<int32_t, bool> flags;
            BE_REFLECT_FIELD HashMap<String, JsonWriteKeyed> keyed;
            BE_REFLECT_FIELD HashMap<String, JsonWriteEmpty> empties;
            BE_REFLECT_FIELD HashMap<String, int32_t> none;
        };

        /// Pairs and variants whose members are written through the same helpers as fields
        struct JsonWritePairsAndVariants {
            BE_CLASS(JsonWritePairsAndVariants)

            BE_REFLECT_FIELD Pair<int32_t, String> numberAndText;
            BE_REFLECT_FIELD Pair<JsonWriteInner, Vec2> objectAndPoint;
            BE_REFLECT_FIELD Pair<Optional<int32_t>, Vector<int32_t>> maybeAndList;
            BE_REFLECT_FIELD Variant<std::monostate, int32_t, String, JsonWriteInner> choice;
            BE_REFLECT_FIELD Variant<std::monostate, int32_t, String, JsonWriteInner> emptyChoice;
            BE_REFLECT_FIELD Variant<Vec2, Vector<int32_t>, Pair<int32_t, int32_t>> shapes;
            BE_REFLECT_FIELD Variant<bool, double> number;
            BE_REFLECT_FIELD Optional<Pair<int32_t, int32_t>> maybePair;
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

        /// A factory base of the pointer cases; abstract, so only the derived classes register
        class JsonWritePtrBase : public RefCounted {
            BE_CLASS(JsonWritePtrBase, FACTORY_BASE, ELEMENT = item)
        public:
            ~JsonWritePtrBase() override = default;
            virtual int32_t Rank() const = 0;

            BE_REFLECT_FIELD String label;
        };

        /// A key that sorts before `type`
        class JsonWritePtrEarly : public JsonWritePtrBase {
            BE_CLASS(JsonWritePtrEarly)
        public:
            int32_t Rank() const override {
                return 1;
            }

            BE_REFLECT_FIELD int32_t alpha = 0;
        };

        /// A key that sorts after `type`
        class JsonWritePtrLate : public JsonWritePtrBase {
            BE_CLASS(JsonWritePtrLate)
        public:
            int32_t Rank() const override {
                return 2;
            }

            BE_REFLECT_FIELD int32_t zeta = 0;
        };

        /// Has a member named `type`: the DOM path lets it replace the class name
        class JsonWritePtrSame : public JsonWritePtrBase {
            BE_CLASS(JsonWritePtrSame)
        public:
            int32_t Rank() const override {
                return 3;
            }

            BE_REFLECT_FIELD String type;
        };

        /// A pointee that is no factory object
        class JsonWritePtrPlain : public RefCounted {
            BE_CLASS(JsonWritePtrPlain)
        public:
            ~JsonWritePtrPlain() override = default;

            BE_REFLECT_FIELD int32_t id = 0;
            BE_REFLECT_FIELD String name;
        };

        class JsonWritePtrPlainChild : public JsonWritePtrPlain {
            BE_CLASS(JsonWritePtrPlainChild)
        public:
            BE_REFLECT_FIELD int32_t extra = 0;
            BE_REFLECT_FIELD int32_t _count = 0;
        };

        struct JsonWritePtrHolder {
            BE_CLASS(JsonWritePtrHolder)

            BE_REFLECT_FIELD IntrusivePtr<JsonWritePtrBase> factory;
            BE_REFLECT_FIELD IntrusivePtr<JsonWritePtrBase> none;
            BE_REFLECT_FIELD IntrusivePtr<JsonWritePtrPlain> plain;
            BE_REFLECT_FIELD IntrusivePtr<JsonWritePtrPlain> nothing;
        };

        /// Sequences of pointers stay on the bridge
        struct JsonWritePtrListHolder {
            BE_CLASS(JsonWritePtrListHolder)

            BE_REFLECT_FIELD Vector<IntrusivePtr<JsonWritePtrBase>> list;
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
