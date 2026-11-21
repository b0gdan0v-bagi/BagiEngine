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
 * `eastl::basic_string<char>`, `PoolString`), reflected enums (the name), an object that has `WriteJson`, a single `IntrusivePtr` of such an object (null writes no member; a factory pointer merges
 * the member `type` into the sorted keys of the object), `Optional` of a native type (an empty one writes no member), and a `Vector` / `SmallVector` / `FixedVector` / `Array` of numbers, strings or
 * objects that have `WriteJson` and at least two fields. **Bridged** (the DOM path through `Ser::Save`, same bytes, allocates): every other field type.
 *
 * Sequences of pointers bridge as well: an element carries `type` and `enabled` and a null one still counts in the array. Why sequences of one-field objects bridge: on the DOM path an element that
 * holds only a scalar `value` is that scalar (`[1,2,3]`), and a streaming writer cannot take a written object back. An element with two or more fields never collapses. Known gap: an element with a
 * field `value` whose other fields are all absent (an empty `Optional`, a null pointer) collapses on the DOM path and is written as an object here.
 *
 * A class with a reflected parent is native: its members merge with its parent's (MemberMerge). A member that writes nothing (an empty `Optional`, a null
 * pointer) hides a member of the same key in a parent, and in a factory object the `type` the pointer writes, where the DOM path keeps that one.
 *
 * This header must stay light, it is included by every generated file: the Json type lives in JsonWriteReflect.cpp, never include Json.h here.
 */

namespace BECore::JsonWrite {

    class MemberMerge;

    /// The object writes itself into a JsonWriter (a generated `WriteJson`)
    template <typename T>
    concept HasWriteJson = requires(const T& value, JsonWriter& writer) {
        value.WriteJson(writer);
    };

    /// The class writes its members into an open object (a `BE_CLASS`; a `BE_EVENT` has none)
    template <typename T>
    concept HasWriteJsonMembers = requires(const T& value, JsonWriter& writer, MemberMerge* outer) {
        value.WriteJsonMembers(writer, outer);
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

    /// A key that a level and the level that asks have both: whose member is written
    CORE_ENUM(MergeTie, uint8_t, ThisLevel, Caller)

    /**
     * @brief The members of one derived level of a class chain, waiting to be written between those of the classes above it.
     *
     * `JsonSerializer` of a class with a reflected parent gets one object: the parent's members first, then the child's, by key, so the keys come out
     * sorted over the whole chain and a key that child and parent both have holds the child's value. A streaming writer cannot sort, so the
     * generated `WriteJsonMembers` of the root class (the one with no reflected parent) writes its sorted members, and before each one asks its
     * @c outer (the level just below it) to write what sorts before that key. The level of a derived class holds its sorted keys and a
     * function that writes member `i`; it passes itself up the chain by a qualified call of the parent's `WriteJsonMembers`.
     *
     * The root level asks about its keys in increasing order, so each level walks its members once. The merge lives on the stack of the call: nothing allocates.
     * Known gap: a member that writes nothing (an empty `Optional`, a null pointer) still hides the same key of a parent,
     * where the DOM path would keep the parent's.
     */
    class MemberMerge {
    public:
        using WriteMember = FunctionRef<void(JsonWriter&, size_t)>;

        /// @p keys are the bare names of this level, sorted and unique; @p write writes member `i` of them as `SaveField` does. A derived class's level
        /// keeps its ties; the `type` of a factory pointer yields them (the DOM path writes it first and the object's own member replaces it)
        MemberMerge(const StringView* keys, size_t count, WriteMember write, MemberMerge* outer, MergeTie tie = MergeTie::ThisLevel)
            : _keys(keys), _count(count), _write(write), _outer(outer), _tie(tie) {}

        MemberMerge(const MemberMerge&) = delete;
        MemberMerge& operator=(const MemberMerge&) = delete;

        /**
         * @brief Writes every pending member of this level and of the levels below it whose key sorts before @p limit.
         *
         * @return false when a pending member has exactly @p limit: it was written here (the most derived one) and the caller skips its own
         */
        bool Before(JsonWriter& w, StringView limit);

        /// Writes what is left of this level and of the levels below it, in key order
        void Finish(JsonWriter& w);

    private:
        /// Writes member `_next` unless a level below holds the same key, which wins; the keys below it that sort first are written either way
        void WriteNext(JsonWriter& w);

        const StringView* _keys;
        size_t _count;
        WriteMember _write;
        MemberMerge* _outer;
        MergeTie _tie;
        size_t _next = 0;
    };

    /// Writes the member @p key of the open object from what @p save writes with `Ser::Save` into a `JsonSerializer`, as the DOM path would; nothing when
    /// the member is absent (an empty `Optional`, a null pointer). @p key is the bare name. A writer that is not valid is not asked again.
    void WriteSerializedMember(JsonWriter& w, StringView key, FunctionRef<void(ISerializer&)> save);

    /// Writes the object @p serialize saves (`Serialize(s)` of a class) as the next value, as the DOM path would
    void WriteSerializedObject(JsonWriter& w, FunctionRef<void(ISerializer&)> serialize);

    namespace Detail {

        /// A single `IntrusivePtr`: nothing for null; the object of the dynamic type (`WriteJson` is virtual); for a factory pointer the same with the member
        /// `type` merged into the sorted keys, as `DataAccessor` writes it. A pointee with no writer of its own is bridged.
        template <typename Ptr>
        void SavePointer(JsonWriter& w, StringView keyLiteral, StringView key, const Ptr& ptr) {
            using Pointee = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;
            if constexpr (::BECore::Detail::HasInnerFactory<Ptr> && HasWriteJsonMembers<Pointee>) {
                if (ptr) {
                    static constexpr StringView kTypeKey = "type";
                    const StringView typeName = ptr->GetTypeMeta().typeName;
                    const auto writeType = [typeName](JsonWriter& writer, size_t) {
                        writer.KeyLiteral("\"type\":");
                        writer.Value(typeName);
                    };
                    MemberMerge typeMerge(&kTypeKey, 1, writeType, nullptr, MergeTie::Caller);
                    w.KeyLiteral(keyLiteral);
                    w.BeginObject();
                    ptr->WriteJsonMembers(w, &typeMerge);
                    w.EndObject();
                }
            } else if constexpr (!::BECore::Detail::HasInnerFactory<Ptr> && HasWriteJson<Pointee>) {
                if (ptr) {
                    w.KeyLiteral(keyLiteral);
                    ptr->WriteJson(w);
                }
            } else {
                WriteSerializedMember(w, key, [&](ISerializer& s) { Ser::Save(s, ptr, key); });
            }
        }

    }  // namespace Detail

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
        } else if constexpr (::BECore::Detail::IsIntrusivePtr<T>) {
            Detail::SavePointer(w, keyLiteral, key, value);
        } else {
            WriteSerializedMember(w, key, [&](ISerializer& s) { Ser::Save(s, value, key); });
        }
    }

}  // namespace BECore::JsonWrite
