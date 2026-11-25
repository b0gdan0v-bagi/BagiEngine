#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Utils/StringUtils.h>
#include <EASTL/fixed_hash_map.h>
#include <EASTL/fixed_string.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/map.h>
#include <EASTL/unordered_map.h>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace BECore {

    /**
     * @brief What a value of a reflected field is, as the schema names it
     *
     * `Void` is the result of a method that returns nothing and the empty alternative of a Variant (`std::monostate`).
     * `Unknown` is a type of a method signature the reflection cannot read or write (a pointer, an interface reference).
     * `Value` is a `Value<T>` field (`Values/Value.h`): `element` is `T`. It is appended after `Unknown` so no ordinal of the older kinds moves.
     */
    CORE_ENUM(TypeKind, uint8_t, Bool, Int8, Int16, Int32, Int64, UInt8, UInt16, UInt32, UInt64, Float, Double, String, Enum, Object, List, Map, Optional, Variant, Pair, Pointer, Opaque, Void,
              Unknown, Value)

    /// The lower-case name of a kind in the schema (`int32`, `string`, `list`, ...).
    StringView TypeKindName(TypeKind kind);

    /**
     * @brief The type of a reflected field or method parameter as data
     *
     * Plain constexpr data with static storage: built from the type list `Ser::DataAccessor` supports, no allocation, no RTTI.
     * A type refers to another type by pointer to its own descriptor, except a reflected object, which is named by its `ClassMeta`
     * (`objectMeta`): a recursive type (the UI tree) is then no recursion in the descriptor, and the class's own registration, not
     * this header, says what its fields are.
     *
     * Compare two descriptors with `SameType`, never by address: an `inline` variable can exist once per shared library.
     */
    struct TypeDesc {
        TypeKind kind = TypeKind::Unknown;
        /// List: the number of elements can change (false for an Array). Map: true.
        bool resizable = false;
        /// List (a FixedVector or an Array), Map (a FixedHashMap), String (a FixedString) that cannot grow: the most it holds;
        /// 0 when there is no limit
        size_t capacity = 0;
        /// List: `sizeof` of the element, the stride of a loop over the list (0 for every other kind). Not part of `SameType`: the
        /// element's own descriptor is.
        size_t elementSize = 0;
        /// String: the value is a `PoolString`, so writing it puts the text into the pool, which never shrinks. Not set on a container of them: look at its element
        bool interns = false;
        /// String: the value is a `String` (the engine's own string), so code that holds only this descriptor may read it in place as one. A `FixedString` is told
        /// from it by nothing else (same kind, same capacity when it can overflow), which is why `SameType` does not look at this member
        bool isEngineString = false;
        /// List: the element. Optional: the value. Map: the value. Pair: the second member. Value: the `T` of `Value<T>`.
        const TypeDesc* element = nullptr;
        /// Map: the key. Pair: the first member.
        const TypeDesc* key = nullptr;
        /// Variant: the alternatives, in index order
        Span<const TypeDesc* const> alternatives{};
        /// Enum: the names of the values, in declaration order
        Span<const StringView> names{};
        /// Enum: `sizeof` of the enum (its underlying type); 0 for every other kind. Part of the identity: two enums of the same
        /// names and different underlying types are two types, and a reader of the field by offset needs the width.
        size_t enumBytes = 0;
        /// Object and Pointer: the `ClassMeta` of the (pointee) type, null for an Opaque type or a pointee that is no `BE_CLASS`.
        /// A function, not a value: the type's own `.gen.hpp` may be in another translation unit than the field's, so a value
        /// read here could differ between translation units. For the same reason a Pointer does not say whether its pointee has an
        /// `AbstractFactory`: the type registry knows the factory bases.
        const ClassMeta& (*objectMeta)() = nullptr;
        /// Opaque and Value: `sizeof` of the type and a hash of its name as the compiler spells it (0 for a kind that is told apart by other members).
        /// Without them every self-serializing type (`Vec2`, `Vec3`, `Color`) would be one type to `SameType`. Never an address:
        /// equal in every translation unit and every shared library.
        size_t opaqueSize = 0;
        uint64_t opaqueHash = 0;
        /// Opaque and Value: the name the hash was made of, for messages only (its address is not the same in every translation unit, so `SameType` never looks at it)
        StringView opaqueName{};

        /// The name of the class of an Object or a Pointer; empty when the type has none.
        [[nodiscard]] StringView ObjectName() const {
            return objectMeta != nullptr ? objectMeta().typeName : StringView{};
        }

        /// The hash of the class name of an Object or a Pointer (the key of `TypeRegistry::Find`); 0 when the type has none.
        [[nodiscard]] uint64_t ObjectHash() const {
            return objectMeta != nullptr ? objectMeta().typeHash : 0;
        }
    };

    /// Whether two descriptors describe the same type: kind, element, key, alternatives, enum names and size, class hash, opaque identity and limits.
    bool SameType(const TypeDesc& a, const TypeDesc& b);

    namespace Detail {

        // The type list below mirrors Ser::DataAccessor (Reflection/DataAccessor.h), one shape per specialization; TypeDescTest holds
        // that Describable<T> and Ser::HasDataAccessor<T> agree. A new accessor needs a shape here.

        template <typename T>
        struct TextTraits {
            static constexpr bool kMatch = false;
        };

        template <typename Alloc>
        struct TextTraits<eastl::basic_string<char, Alloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            static constexpr size_t kCapacity = 0;
        };

        template <int N, bool Overflow, typename OverflowAlloc>
        struct TextTraits<eastl::fixed_string<char, N, Overflow, OverflowAlloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            static constexpr size_t kCapacity = Overflow ? 0 : static_cast<size_t>(N);
        };

        template <>
        struct TextTraits<PoolString> {
            static constexpr bool kMatch = true;
            static constexpr size_t kCapacity = 0;
        };

        template <>
        struct TextTraits<TextView> {
            static constexpr bool kMatch = true;
            static constexpr size_t kCapacity = 0;
        };

        template <typename T>
        struct ListTraits {
            static constexpr bool kMatch = false;
        };

        template <typename Elem, typename Alloc>
        struct ListTraits<eastl::vector<Elem, Alloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            using Element = Elem;
            static constexpr bool kResizable = true;
            static constexpr size_t kCapacity = 0;
        };

        template <typename Elem, size_t N, bool Overflow, typename Alloc>
        struct ListTraits<eastl::fixed_vector<Elem, N, Overflow, Alloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            using Element = Elem;
            static constexpr bool kResizable = true;
            static constexpr size_t kCapacity = Overflow ? 0 : N;
        };

        template <typename Elem, size_t N>
        struct ListTraits<Array<Elem, N>> {
            static constexpr bool kMatch = true;
            using Element = Elem;
            static constexpr bool kResizable = false;
            static constexpr size_t kCapacity = N;
        };

        template <typename T>
        struct MapTraits {
            static constexpr bool kMatch = false;
        };

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc>
        struct MapTraits<eastl::unordered_map<K, V, Hash, Pred, Alloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            using Key = K;
            using Value = V;
            static constexpr size_t kCapacity = 0;
        };

        template <typename K, typename V, size_t NodeCount, size_t BucketCount, bool Overflow, typename Hash, typename Pred, bool CacheHashCode, typename OverflowAlloc>
        struct MapTraits<eastl::fixed_hash_map<K, V, NodeCount, BucketCount, Overflow, Hash, Pred, CacheHashCode, OverflowAlloc>> {
            static constexpr bool kMatch = true;
            using Key = K;
            using Value = V;
            static constexpr size_t kCapacity = Overflow ? 0 : NodeCount;
        };

        template <typename K, typename V, typename Cmp, typename Alloc>
        struct MapTraits<eastl::map<K, V, Cmp, Alloc>> {  //-NoStd: the specialization names the type it matches, as Ser::DataAccessor does
            static constexpr bool kMatch = true;
            using Key = K;
            using Value = V;
            static constexpr size_t kCapacity = 0;
        };

        template <typename T>
        struct OptionalTraits {
            static constexpr bool kMatch = false;
        };

        template <typename T>
        struct OptionalTraits<Optional<T>> {
            static constexpr bool kMatch = true;
            using Value = T;
        };

        template <typename T>
        struct PairTraits {
            static constexpr bool kMatch = false;
        };

        template <typename A, typename B>
        struct PairTraits<Pair<A, B>> {
            static constexpr bool kMatch = true;
            using First = A;
            using Second = B;
        };

        template <typename T>
        struct VariantTraits {
            static constexpr bool kMatch = false;
        };

        template <typename... Ts>
        struct VariantTraits<Variant<Ts...>> {
            static constexpr bool kMatch = true;
        };

        template <typename T>
        struct PointerTraits {
            static constexpr bool kMatch = false;
        };

        template <typename T>
        struct PointerTraits<IntrusivePtr<T>> {
            static constexpr bool kMatch = true;
            using Pointee = T;
        };

        template <typename T>
        concept IsTextType = TextTraits<T>::kMatch;
        template <typename T>
        concept IsListType = ListTraits<T>::kMatch;
        template <typename T>
        concept IsMapType = MapTraits<T>::kMatch;
        template <typename T>
        concept IsOptionalType = OptionalTraits<T>::kMatch;
        template <typename T>
        concept IsPairType = PairTraits<T>::kMatch;
        template <typename T>
        concept IsVariantType = VariantTraits<T>::kMatch;
        template <typename T>
        concept IsPointerType = PointerTraits<T>::kMatch;

        /// A type with a `Serialize` / `Deserialize` pair that is not a pointer: a reflected class, or any other type that writes itself
        template <typename T>
        concept IsSelfSerializing = !std::is_arithmetic_v<T> && !std::is_enum_v<T> && !IsPointerType<T> && requires(const T& constValue, T& value, ISerializer& writer, IDeserializer& reader) {
            constValue.Serialize(writer);
            value.Deserialize(reader);
        };

        /// A `Value<T>` (`Values/Value.h`, which this header does not include): it names itself by `ValueMarker` and its `T` by `ValueOf`
        template <typename T>
        concept IsValueField = requires {
            typename T::ValueMarker;
            typename T::ValueOf;
        };

        /// A `BE_CLASS` / `BE_EVENT` type: the macro declares `GetStaticTypeMeta`, whatever translation unit defines it
        template <typename T>
        concept HasStaticMeta = requires {
            {T::GetStaticTypeMeta()}->std::convertible_to<const ClassMeta&>;
        };

        /// The name of `T` as the compiler spells it, cut out of the signature text of this function (`BECore::Vec2`)
        template <typename T>
        consteval auto OpaqueTypeNameText() {
#if defined(__clang__)
            // auto BECore::Detail::OpaqueTypeNameText() [T = BECore::Vec2]
            constexpr std::string_view sv{__PRETTY_FUNCTION__};  //-NoStd: eastl::string_view::find_last_of is not constexpr
            constexpr size_t last = sv.find_last_of(']');
            constexpr size_t first = sv.find("T = ") + 4;
#elif defined(_MSC_VER)
            // auto __cdecl BECore::Detail::OpaqueTypeNameText<struct BECore::Vec2>(void)
            constexpr std::string_view sv{__FUNCSIG__};  //-NoStd: as above  // NOLINT(clang-diagnostic-language-extension-token)
            constexpr size_t last = sv.find_last_of('>');
            constexpr size_t first = sv.find("NameText<") + 9;
#else
            // consteval auto BECore::Detail::OpaqueTypeNameText() [with T = BECore::Vec2]
            constexpr std::string_view sv{__PRETTY_FUNCTION__};  //-NoStd: as above
            constexpr size_t last = sv.find_last_of(']');
            constexpr size_t first = sv.find("T = ") + 4;
#endif
            Array<char, last - first + 1> buf{};  // +1 '\0' terminated
            for (size_t i = 0; i < last - first; ++i) {
                buf[i] = sv[first + i];
            }
            return buf;
        }

        template <typename T>
        inline constexpr auto kOpaqueTypeName{OpaqueTypeNameText<T>()};

        /// The name of `T`, in static storage; the identity of an opaque type is the hash of it (`TypeDesc::opaqueHash`)
        template <typename T>
        constexpr StringView OpaqueTypeName() {
            return StringView{kOpaqueTypeName<T>.data(), kOpaqueTypeName<T>.size() - 1};
        }

        template <typename T>
        struct DescHolder;

        template <typename... Ts>
        struct AlternativeTable {
            static constexpr const TypeDesc* kTable[] = {&DescHolder<Ts>::kValue...};
        };

        template <typename T>
        constexpr TypeKind ArithmeticKind() {
            if constexpr (std::is_same_v<T, bool>) {
                return TypeKind::Bool;
            } else if constexpr (std::is_floating_point_v<T>) {
                return sizeof(T) == sizeof(float) ? TypeKind::Float : TypeKind::Double;
            } else if constexpr (std::is_signed_v<T>) {
                return sizeof(T) == 1 ? TypeKind::Int8 : sizeof(T) == 2 ? TypeKind::Int16 : sizeof(T) == 4 ? TypeKind::Int32 : TypeKind::Int64;
            } else {
                return sizeof(T) == 1 ? TypeKind::UInt8 : sizeof(T) == 2 ? TypeKind::UInt16 : sizeof(T) == 4 ? TypeKind::UInt32 : TypeKind::UInt64;
            }
        }

        template <typename Variant>
        struct VariantAlternatives;

        template <typename... Ts>
        struct VariantAlternatives<Variant<Ts...>> {
            static constexpr Span<const TypeDesc* const> Get() {
                return Span<const TypeDesc* const>(AlternativeTable<Ts...>::kTable, sizeof...(Ts));
            }
        };

    }  // namespace Detail

    /**
     * @brief The types `DescribeType` knows: the ones `Ser::DataAccessor` has an accessor for
     *
     * A type of another kind (`HashSet`, `UniquePtr`, an unreflected enum, a raw pointer) is not describable.
     */
    template <typename T>
    concept Describable = std::is_arithmetic_v<T> || Detail::IsTextType<T> || IsReflectedEnum<T> || Detail::IsListType<T> || Detail::IsMapType<T> || Detail::IsOptionalType<T> ||
                          Detail::IsPairType<T> || Detail::IsVariantType<T> || Detail::IsPointerType<T> || Detail::IsSelfSerializing<T> || Ser::HasDataAccessor<T> || std::is_same_v<T, std::monostate>;

    namespace Detail {

        template <typename T>
        constexpr TypeDesc MakeDesc() {
            TypeDesc desc;
            if constexpr (std::is_arithmetic_v<T>) {
                desc.kind = ArithmeticKind<T>();
            } else if constexpr (IsTextType<T>) {
                desc.kind = TypeKind::String;
                desc.capacity = TextTraits<T>::kCapacity;
                desc.interns = std::is_same_v<T, PoolString>;
                desc.isEngineString = std::is_same_v<T, String>;
            } else if constexpr (IsReflectedEnum<T>) {
                desc.kind = TypeKind::Enum;
                const auto& enumNames = EnumUtils<T>::Names();
                desc.names = Span<const StringView>(enumNames.data(), enumNames.size());
                desc.enumBytes = sizeof(T);
            } else if constexpr (IsListType<T>) {
                desc.kind = TypeKind::List;
                desc.resizable = ListTraits<T>::kResizable;
                desc.capacity = ListTraits<T>::kCapacity;
                desc.elementSize = sizeof(typename ListTraits<T>::Element);
                desc.element = &DescHolder<typename ListTraits<T>::Element>::kValue;
            } else if constexpr (IsMapType<T>) {
                desc.kind = TypeKind::Map;
                desc.resizable = true;
                desc.capacity = MapTraits<T>::kCapacity;
                desc.key = &DescHolder<typename MapTraits<T>::Key>::kValue;
                desc.element = &DescHolder<typename MapTraits<T>::Value>::kValue;
            } else if constexpr (IsOptionalType<T>) {
                desc.kind = TypeKind::Optional;
                desc.element = &DescHolder<typename OptionalTraits<T>::Value>::kValue;
            } else if constexpr (IsPairType<T>) {
                desc.kind = TypeKind::Pair;
                desc.key = &DescHolder<typename PairTraits<T>::First>::kValue;
                desc.element = &DescHolder<typename PairTraits<T>::Second>::kValue;
            } else if constexpr (IsVariantType<T>) {
                desc.kind = TypeKind::Variant;
                desc.alternatives = VariantAlternatives<T>::Get();
            } else if constexpr (IsPointerType<T>) {
                using Pointee = typename PointerTraits<T>::Pointee;
                desc.kind = TypeKind::Pointer;
                if constexpr (HasStaticMeta<Pointee>) {
                    desc.objectMeta = &Pointee::GetStaticTypeMeta;
                }
            } else if constexpr (std::is_same_v<T, std::monostate>) {
                desc.kind = TypeKind::Void;
            } else {
                // A type that is no class of its own but has a `Ser::DataAccessor` specialization (`UIEdges`, `UIColor`) is as opaque as a `Serialize` pair
                static_assert(IsSelfSerializing<T> || Ser::HasDataAccessor<T>, "The type has no Ser::DataAccessor, so it has no TypeDesc (Reflection/TypeDesc.h)");
                if constexpr (IsSelfSerializing<T> && HasStaticMeta<T>) {
                    desc.kind = TypeKind::Object;
                    desc.objectMeta = &T::GetStaticTypeMeta;
                } else {
                    desc.kind = IsValueField<T> ? TypeKind::Value : TypeKind::Opaque;
                    if constexpr (IsValueField<T>) {
                        desc.element = &DescHolder<typename T::ValueOf>::kValue;
                    }
                    desc.opaqueSize = sizeof(T);
                    desc.opaqueName = OpaqueTypeName<T>();
                    desc.opaqueHash = StringUtils::GetHash(desc.opaqueName);
                }
            }
            return desc;
        }

        template <typename T>
        struct DescHolder {
            static constexpr TypeDesc kValue = MakeDesc<T>();
        };

        inline constexpr TypeDesc kVoidDesc = [] {
            TypeDesc desc;
            desc.kind = TypeKind::Void;
            return desc;
        }();

        inline constexpr TypeDesc kUnknownDesc{};

    }  // namespace Detail

    /**
     * @brief The descriptor of a type a reflected field can have; static storage, so the reference never dangles
     *
     * @example
     * static_assert(DescribeType<Vector<int32_t>>().kind == TypeKind::List);
     * static_assert(DescribeType<Vector<int32_t>>().element->kind == TypeKind::Int32);
     */
    template <typename T>
    requires Describable<std::remove_cv_t<T>> constexpr const TypeDesc& DescribeType() {
        return Detail::DescHolder<std::remove_cv_t<T>>::kValue;
    }

    /**
     * @brief The descriptor of a method's parameter or result type: `Void` for `void`, `Unknown` for a type that is not describable
     *
     * References and `const` are removed first, so `const String&` is a String.
     */
    template <typename T>
    constexpr const TypeDesc& DescribeSignatureType() {
        using Plain = std::remove_cvref_t<T>;
        if constexpr (std::is_void_v<Plain>) {
            return Detail::kVoidDesc;
        } else if constexpr (Describable<Plain>) {
            return DescribeType<Plain>();
        } else {
            return Detail::kUnknownDesc;
        }
    }

}  // namespace BECore
