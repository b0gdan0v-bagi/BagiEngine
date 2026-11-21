#pragma once

#include <BECore/Control/JsonWriter.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/FunctionRef.h>
#include <concepts>

/**
 * @file JsonWriteReflect.h
 * @brief What the generated `WriteJson` of a `BE_CLASS` / `BE_EVENT` calls: one reflected field written straight into a JsonWriter.
 *
 * The text is the one `JsonSerializer` + `DumpJson` give for the same value (keys sorted, the shapes of Reflection/CLAUDE.md, "JSON shapes of
 * the reflection pair"); the generator sorts the keys, these helpers write the values.
 *
 * **Native** (nothing allocates beyond the growth of the caller's String): numbers and `bool`, strings (`String`, `FixedString`, any
 * `eastl::basic_string<char>`, `PoolString`), reflected enums (the name), an object that has `WriteJson`, `Optional` of a native type (an empty one
 * writes no member), and a `Vector` / `SmallVector` / `FixedVector` / `Array` of numbers, strings or objects that have `WriteJson` and at least two
 * fields. **Bridged** (the DOM path through `Ser::Save`, same bytes, allocates): every other field type.
 *
 * Why sequences of one-field objects bridge: on the DOM path an element that holds only a scalar `value` is that scalar (`[1,2,3]`), and a
 * streaming writer cannot take a written object back. An element with two or more fields never collapses. Known gap: an element with a field `value` whose other
 * fields are all absent (an empty `Optional`, a null pointer) collapses on the DOM path and is written as an object here.
 *
 * This header must stay light, it is included by every generated file: the Json type lives in JsonWriteReflect.cpp, never include Json.h here.
 */

namespace BECore::JsonWrite {

    /// The object writes itself into a JsonWriter (a generated `WriteJson`)
    template <typename T>
    concept HasWriteJson = requires(const T& value, JsonWriter& writer) {
        value.WriteJson(writer);
    };

    /// The class declares how many fields it has (BE_CLASS and BE_EVENT do)
    template <typename T>
    concept HasStaticFieldCount = requires {
        {T::GetStaticFieldCount()}->std::convertible_to<size_t>;
    };

    /// The members of `JsonSerializer`'s Write set. A type outside it (a `char`, a `long` on a platform where it is no `int64_t`) does not compile on the DOM
    /// path either, so it is not widened here
    template <typename T>
    concept IsJsonNumber = std::same_as<T, bool> || std::same_as<T, int8_t> || std::same_as<T, uint8_t> || std::same_as<T, int16_t> || std::same_as<T, uint16_t> || std::same_as<T, int32_t> ||
                           std::same_as<T, uint32_t> || std::same_as<T, int64_t> || std::same_as<T, uint64_t> || std::same_as<T, float> || std::same_as<T, double>;

    namespace Detail {

        /// An object of fewer fields may serialize to the single member `value`, which the DOM path writes as a scalar
        inline constexpr size_t kFieldsThatNeverCollapse = 2;

        /// Written as one bare value: a number, a bool, a string of any kind (`TextTraits` is the list of the accessors, `PoolString` included)
        template <typename T>
        concept IsScalar = IsJsonNumber<T> || ::BECore::Detail::TextTraits<T>::kMatch;

        template <typename T>
        concept IsObjectElement = HasWriteJson<T> && HasStaticFieldCount<T>;

        template <typename T>
        void WriteScalar(JsonWriter& w, const T& value) {
            if constexpr (IsJsonNumber<T> || std::same_as<T, PoolString>) {
                w.Value(value);
            } else {
                w.Value(StringView(value.data(), value.size()));
            }
        }

    }  // namespace Detail

    /// Writes the member @p key of the open object from what @p save writes with `Ser::Save` into a `JsonSerializer`, as the DOM path would; nothing when
    /// the member is absent (an empty `Optional`, a null pointer). @p key is the bare name. A writer that is not valid is not asked again.
    void WriteSerializedMember(JsonWriter& w, StringView key, FunctionRef<void(ISerializer&)> save);

    /// Writes the object @p serialize saves (`Serialize(s)` of a class) as the next value, as the DOM path would
    void WriteSerializedObject(JsonWriter& w, FunctionRef<void(ISerializer&)> serialize);

    /**
     * @brief Writes the member of the open object that holds @p value
     *
     * @param keyLiteral The ready key, the quoted name and its colon (`"count":`); a generator holds it as a constant
     * @param key The bare name, which only the bridge needs
     */
    template <typename T>
    void SaveField(JsonWriter& w, StringView keyLiteral, StringView key, const T& value) {
        if constexpr (Detail::IsScalar<T>) {
            w.KeyLiteral(keyLiteral);
            Detail::WriteScalar(w, value);
        } else if constexpr (IsReflectedEnum<T>) {
            w.KeyLiteral(keyLiteral);
            w.Value(EnumUtils<T>::ToString(value));
        } else if constexpr (HasWriteJson<T>) {
            w.KeyLiteral(keyLiteral);
            value.WriteJson(w);
        } else if constexpr (::BECore::Detail::OptionalTraits<T>::kMatch) {
            if (value.has_value()) {
                SaveField(w, keyLiteral, key, *value);
            }
        } else if constexpr (::BECore::Detail::ListTraits<T>::kMatch) {
            using Element = typename ::BECore::Detail::ListTraits<T>::Element;
            const auto saveBridged = [&] { WriteSerializedMember(w, key, [&](ISerializer& s) { Ser::Save(s, value, key); }); };
            if constexpr (Detail::IsScalar<Element>) {
                w.KeyLiteral(keyLiteral);
                w.BeginArray();
                for (const Element& element : value) {
                    Detail::WriteScalar(w, element);
                }
                w.EndArray();
            } else if constexpr (Detail::IsObjectElement<Element>) {
                if (Element::GetStaticFieldCount() >= Detail::kFieldsThatNeverCollapse) {
                    w.KeyLiteral(keyLiteral);
                    w.BeginArray();
                    for (const Element& element : value) {
                        element.WriteJson(w);
                    }
                    w.EndArray();
                } else {
                    saveBridged();
                }
            } else {
                saveBridged();
            }
        } else {
            WriteSerializedMember(w, key, [&](ISerializer& s) { Ser::Save(s, value, key); });
        }
    }

}  // namespace BECore::JsonWrite
