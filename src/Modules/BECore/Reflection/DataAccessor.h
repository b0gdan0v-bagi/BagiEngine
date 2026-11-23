#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/FactoryTraits.h>
#include <BECore/Reflection/FieldAttributes.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <BECore/Utils/EnumUtils.h>
#include <EASTL/array.h>
#include <EASTL/fixed_hash_map.h>
#include <EASTL/map.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>
#include <utility>
#include <variant>

namespace BECore {

    // =========================================================================
    // Internal helpers
    // =========================================================================

    namespace Detail {

        template <typename Ptr>
        struct IntrusivePtrPointeeType {};

        template <typename T>
        struct IntrusivePtrPointeeType<IntrusivePtr<T>> {
            using type = T;
        };

        template <typename Ptr>
        concept IsIntrusivePtr = requires {
            typename IntrusivePtrPointeeType<Ptr>::type;
        };

        template <typename Ptr>
        concept HasInnerFactory = IsIntrusivePtr<Ptr> && HasFactory<typename IntrusivePtrPointeeType<Ptr>::type>;

        template <typename T>
        concept HasInitialize = requires(T & t) {
            t.Initialize();
        };

        /// Element-scope hook: `bool Initialize(IDeserializer&)` (the IWidget family has it). See Ser::Detail::LoadElement.
        template <typename T>
        concept HasInitializeFrom = requires(T & t, IDeserializer& d) {
            {t.Initialize(d)}->std::convertible_to<bool>;
        };

        template <typename T, typename Archive>
        concept HasSerialize = requires(const T& t, Archive& a) {
            t.Serialize(a);
        };

        template <typename T, typename Archive>
        concept HasDeserialize = requires(T & t, Archive& a) {
            t.Deserialize(a);
        };

    }  // namespace Detail

    // =========================================================================
    // DataAccessor<T> -- primary template (declared only: a type without a specialization is not reflected)
    // =========================================================================

    namespace Ser {

        /// A type with no specialization is not reflected: using it is a compile error ("implicit instantiation of undefined template
        /// DataAccessor<T>"), and HasDataAccessor<T> is false (it is declared and never defined, so asking for its size is no error).
        template <typename T, typename = void>
        struct DataAccessor;

        template <typename T>
        concept HasDataAccessor = requires {
            sizeof(DataAccessor<T>);
        };

        // =================================================================
        // Variant body: the `index` attribute and the active alternative under `value`, in the scope the caller opened
        // (a field's own object, or an element of an array or a map). Defined before SaveElement / LoadElement, which call it:
        // with a std::variant argument, ADL looks in namespace std only, so a later declaration here would not be found.
        // =================================================================

        namespace Detail {

            template <typename T>
            struct IsVariantType : std::false_type {};

            template <typename... Ts>
            struct IsVariantType<std::variant<Ts...>> : std::true_type {};

            template <typename T>
            concept IsVariant = IsVariantType<T>::value;

            inline constexpr StringView kVariantIndexKey = "index";
            inline constexpr StringView kVariantValueKey = "value";

            /// The index is an int32_t, not unsigned, so that a negative text in a lenient XML read is a bad index and not alternative 0
            using VariantIndex = int32_t;

            template <typename... Ts>
            constexpr void CheckVariantAlternatives() {
                static_assert((HasDataAccessor<Ts> && ...), "Every alternative of a reflected Variant needs a Ser::DataAccessor (Reflection/DataAccessor.h)");
                static_assert((std::is_default_constructible_v<Ts> && ...),
                              "Every alternative of a reflected Variant must be default constructible: a load emplaces the alternative its index names, then reads it");
            }

            template <typename Var, size_t... I>
            void SaveVariantAlternative(ISerializer& s, const Var& value, std::index_sequence<I...> /*indices*/) {
                const size_t active = value.index();
                static_cast<void>(((active == I ? (DataAccessor<std::variant_alternative_t<I, Var>>::Save(s, std::get<I>(value), kVariantValueKey), true) : false) || ...));
            }

            template <typename Var, size_t... I>
            bool LoadVariantAlternative(IDeserializer& d, Var& value, size_t index, std::index_sequence<I...> /*indices*/) {
                bool loaded = false;
                static_cast<void>(((index == I ? (loaded = DataAccessor<std::variant_alternative_t<I, Var>>::Load(d, value.template emplace<I>(), kVariantValueKey), true) : false) || ...));
                return loaded;
            }

            template <typename... Ts>
            void SaveVariantBody(ISerializer& s, const std::variant<Ts...>& value) {
                CheckVariantAlternatives<Ts...>();
                s.WriteAttribute(kVariantIndexKey, static_cast<VariantIndex>(value.index()));
                SaveVariantAlternative(s, value, std::index_sequence_for<Ts...>{});
            }

            /// False, with the variant as it was, when the index is missing or out of range; otherwise the result of the alternative's Load
            template <typename... Ts>
            bool LoadVariantBody(IDeserializer& d, std::variant<Ts...>& value) {
                CheckVariantAlternatives<Ts...>();
                VariantIndex index = 0;
                if (!d.ReadAttribute(kVariantIndexKey, index)) {
                    // A key that is there and failed to read was reported by the reader
                    if (!d.HasKey(kVariantIndexKey)) {
                        d.ReportError(kVariantIndexKey, "Variant index is missing");
                    }
                    return false;
                }
                if (index < 0 || static_cast<size_t>(index) >= sizeof...(Ts)) {
                    d.ReportError(kVariantIndexKey, Format("Invalid variant index {}", index));
                    return false;
                }
                return LoadVariantAlternative(d, value, static_cast<size_t>(index), std::index_sequence_for<Ts...>{});
            }

        }  // namespace Detail

        // =================================================================
        // Internal element helpers (used by vector/map specializations)
        // =================================================================

        namespace Detail {

            template <typename T>
            void SaveElement(ISerializer& s, const T& item) {
                if constexpr (IsVariant<T>) {
                    SaveVariantBody(s, item);
                } else if constexpr (::BECore::Detail::IsIntrusivePtr<T>) {
                    if (item) {
                        using Pointee = typename ::BECore::Detail::IntrusivePtrPointeeType<T>::type;
                        if constexpr (HasFactory<Pointee>) {
                            s.WriteAttribute("type", item->GetTypeMeta().typeName);
                        } else if constexpr (requires { item->GetTypeMeta(); }) {
                            // A base that has no FactoryTraits (IComponent) but whose derived types register themselves: the reader
                            // recreates the element by `type`, so a file without it would load as the base class
                            if (AbstractFactory<Pointee>::GetInstance().FindMeta(item->GetTypeMeta().typeName) != nullptr) {
                                s.WriteAttribute("type", item->GetTypeMeta().typeName);
                            }
                        }
                        if constexpr (::BECore::Detail::HasSerialize<Pointee, ISerializer>)
                            item->Serialize(s);
                    }
                } else if constexpr (::BECore::Detail::HasSerialize<T, ISerializer>) {
                    item.Serialize(s);
                } else {
                    s.WriteAttribute("value", item);
                }
            }

            /**
             * @brief Whether the purpose filter of @p d lets the factory type named @p typeText be created
             *
             * Asks the registered ClassMeta before `Create`, so a refused type is never instantiated; the error is
             * reported at the current path. A name the factory does not know passes (the unknown-type error follows).
             */
            template <typename Pointee>
            bool FactoryTypeAllowed(IDeserializer& d, StringView typeText) {
                if (!d.GetPurposeFilter()) {
                    return true;
                }
                const ClassMeta* meta = AbstractFactory<Pointee>::GetInstance().FindMeta(typeText);
                return meta == nullptr || d.AcceptsPurpose(meta->purpose, meta->typeName);
            }

            template <typename T>
            inline constexpr bool IsForeignText = false;

            template <typename Alloc>
            inline constexpr bool IsForeignText<eastl::basic_string<char, Alloc>> = !std::is_same_v<eastl::basic_string<char, Alloc>, String>;

            /**
             * @brief Loads one array element (a value, an object or a factory-created pointer)
             *
             * For an intrusive pointer: creates the object by its `type` attribute when `item` is null, then
             * calls `Deserialize(d)`, then `Initialize()` when the class has it. A class with
             * `bool Initialize(IDeserializer&)` (declared on the pointee type, so a factory base declares it
             * virtual, as IWidget does) has that called too, still inside the element scope (the caller
             * has opened it with BeginArrayElement), so it can read more of its own element. When it returns
             * `false` the error "<TypeName> failed to initialize" is reported under the key `type` and `item`
             * is reset, so LoadVectorLikeFactory drops the element. A `type` the purpose filter of @p d refuses is
             * reported and `item` stays empty (nothing is created or read), so the element is dropped the same way.
             *
             * @note The single-pointer accessors (DataAccessor for IntrusivePtr fields) do not call the
             *       `Initialize(IDeserializer&)` hook: a lone pointer cannot be dropped from a list.
             */
            template <typename T>
            void LoadElement(IDeserializer& d, T& item) {
                if constexpr (IsVariant<T>) {
                    LoadVariantBody(d, item);
                } else if constexpr (::BECore::Detail::IsIntrusivePtr<T>) {
                    using Pointee = typename ::BECore::Detail::IntrusivePtrPointeeType<T>::type;
                    if (!item) {
                        // A view into the reader, used before the element is read: no copy of a long type name per element
                        StringView typeStr;
                        if (d.ReadAttributeView("type", typeStr)) {
                            if (!FactoryTypeAllowed<Pointee>(d, typeStr)) {
                                return;
                            }
                            item = AbstractFactory<Pointee>::GetInstance().Create(typeStr);
                            if (!item && HasFactory<Pointee>)
                                d.ReportError("type", eastl::string_view(Format("Unknown factory type: '{}'", typeStr).c_str()));
                        }
                        if (!item) {
                            if constexpr (!std::is_abstract_v<Pointee>)
                                item = New<Pointee>();
                        }
                    }
                    if (item) {
                        if constexpr (::BECore::Detail::HasDeserialize<Pointee, IDeserializer>)
                            item->Deserialize(d);
                        if constexpr (::BECore::Detail::HasInitialize<Pointee>)
                            item->Initialize();
                        if constexpr (::BECore::Detail::HasInitializeFrom<Pointee>) {
                            if (!item->Initialize(d)) {
                                eastl::string_view typeName = "element";
                                if constexpr (HasFactory<Pointee>)
                                    typeName = item->GetTypeMeta().typeName;
                                d.ReportError("type", eastl::string_view(Format("{} failed to initialize", typeName).c_str()));
                                item = {};
                            }
                        }
                    }
                } else if constexpr (::BECore::Detail::HasDeserialize<T, IDeserializer>) {
                    item.Deserialize(d);
                } else if constexpr (IsForeignText<T>) {
                    // The reader fills a String only; a text of another allocator (ArenaString) takes a copy of what was read
                    String text;
                    if (d.ReadAttribute("value", text)) {
                        item.assign(text.data(), text.size());
                    }
                } else {
                    d.ReadAttribute("value", item);
                }
            }

            template <typename T>
            T MakeElement(IDeserializer& d) {
                T item{};
                LoadElement(d, item);
                return item;
            }

            // -----------------------------------------------------------------
            // Fixed containers that may not overflow: what does not fit is dropped and reported
            // -----------------------------------------------------------------

            /// A fixed container that cannot overflow and has no room for one more element
            template <typename Container>
            bool IsFull(const Container& container) {
                if constexpr (requires { Container::can_overflow(); }) {
                    return !Container::can_overflow() && container.size() >= container.max_size();
                } else {
                    return false;
                }
            }

            /**
             * Makes room for the @p count the reader announced, once, so the loop never grows the block. A fixed container that cannot
             * overflow has its whole capacity already and asserts when asked for more, so it is left alone; the loop drops what does
             * not fit. @p count is an upper bound (a factory array counts disabled and null elements) and is already limited by
             * ReaderLimits::kMaxArrayElements.
             */
            template <typename Container>
            void ReserveForLoad(Container& container, size_t count) {
                if constexpr (requires { Container::can_overflow(); }) {
                    if constexpr (Container::can_overflow()) {
                        container.reserve(count);
                    }
                } else {
                    container.reserve(count);
                }
            }

            /// The data a fixed container had no room for is an error of the field like any bad input (the sizes, not the content)
            inline void ReportDropped(IDeserializer& d, StringView key, size_t kept, size_t offered, StringView unit) {
                d.ReportError(key, Format("Data lost, the fixed container is full: kept {} of {} {}", kept, offered, unit));
            }

            // -----------------------------------------------------------------
            // Vector-like helpers: resizable containers (vector, fixed_vector)
            // -----------------------------------------------------------------

            template <typename Container>
            void SaveVectorLike(ISerializer& s, const Container& vec, eastl::string_view key) {
                size_t count = vec.size();
                if (s.BeginArray(key, "item", count)) {
                    for (const auto& item : vec) {
                        if (s.BeginArrayElement()) {
                            SaveElement(s, item);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            template <typename Container>
            bool LoadVectorLike(IDeserializer& d, Container& vec, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, "item", count)) {
                    vec.clear();
                    ReserveForLoad(vec, count);
                    size_t offered = 0;
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            ++offered;
                            if (!IsFull(vec)) {
                                using Elem = typename Container::value_type;
                                vec.push_back(MakeElement<Elem>(d));
                            } else {
                                d.SkipScope();
                            }
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    if (vec.size() < offered) {
                        ReportDropped(d, key, vec.size(), offered, "items");
                    }
                    return true;
                }
                return false;
            }

            template <typename Container>
            void SaveVectorLikeFactory(ISerializer& s, const Container& vec, eastl::string_view key) {
                using Ptr = typename Container::value_type;
                using Base = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;
                using Traits = FactoryTraits<Base>;

                size_t count = vec.size();
                if (s.BeginArray(key, Traits::elementName, count)) {
                    for (const auto& item : vec) {
                        if (item && s.BeginArrayElement()) {
                            s.WriteAttribute("enabled", true);
                            SaveElement(s, item);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            template <typename Container>
            bool LoadVectorLikeFactory(IDeserializer& d, Container& vec, eastl::string_view key) {
                using Ptr = typename Container::value_type;
                using Base = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;
                using Traits = FactoryTraits<Base>;

                size_t count = 0;
                if (d.BeginArray(key, Traits::elementName, count)) {
                    vec.clear();
                    ReserveForLoad(vec, count);
                    size_t offered = 0;
                    size_t dropped = 0;
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            bool enabled = true;
                            d.ReadAttribute("enabled", enabled);
                            if (enabled) {
                                Ptr item;
                                LoadElement(d, item);
                                if (!item) {
                                    d.SkipScope();
                                } else {
                                    ++offered;
                                    if (IsFull(vec)) {
                                        ++dropped;
                                    } else {
                                        vec.push_back(eastl::move(item));
                                    }
                                }
                            } else {
                                d.SkipScope();
                            }
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    if (dropped > 0) {
                        ReportDropped(d, key, offered - dropped, offered, "items");
                    }
                    return true;
                }
                return false;
            }

            // -----------------------------------------------------------------
            // Fixed-size array helpers (eastl::array)
            // -----------------------------------------------------------------

            template <typename Arr>
            void SaveFixedArray(ISerializer& s, const Arr& arr, eastl::string_view key) {
                size_t count = arr.size();
                if (s.BeginArray(key, "item", count)) {
                    for (const auto& item : arr) {
                        if (s.BeginArrayElement()) {
                            SaveElement(s, item);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            template <typename Arr>
            bool LoadFixedArray(IDeserializer& d, Arr& arr, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, "item", count)) {
                    size_t loadCount = count < arr.size() ? count : arr.size();
                    for (size_t i = 0; i < loadCount; ++i) {
                        if (d.BeginArrayElement()) {
                            LoadElement(d, arr[i]);
                            d.EndArrayElement();
                        }
                    }
                    using Elem = typename Arr::value_type;
                    for (size_t i = loadCount; i < arr.size(); ++i)
                        DefaultMaker<Elem>::MakeDefault(arr[i]);
                    // Elements past the array's size are skipped on purpose: their keys are not reported as unknown keys
                    for (size_t i = loadCount; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            d.SkipScope();
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    return true;
                }
                return false;
            }

        }  // namespace Detail

        // =================================================================
        // Primitives (arithmetic types)
        // =================================================================

        template <typename T>
        struct DataAccessor<T, std::enable_if_t<std::is_arithmetic_v<T>>> {
            static void Save(ISerializer& s, const T& value, eastl::string_view key) {
                s.WriteAttribute(key, value);
            }

            static bool Load(IDeserializer& d, T& value, eastl::string_view key) {
                return d.ReadAttribute(key, value);
            }
        };

        // =================================================================
        // Strings: String, a string of any allocator, FixedString
        // =================================================================

        namespace Detail {

            template <typename Text>
            void SaveText(ISerializer& s, const Text& value, StringView key) {
                s.WriteAttribute(key, StringView(value.data(), value.size()));
            }

            /// The reader fills a String; any other string takes the text only when it was read (a missing attribute leaves the field)
            template <typename Text>
            bool LoadText(IDeserializer& d, Text& value, StringView key) {
                if constexpr (std::is_same_v<Text, String>) {
                    return d.ReadAttribute(key, value);
                } else {
                    String text;
                    if (!d.ReadAttribute(key, text)) {
                        return false;
                    }
                    const size_t dropped = ::BECore::Detail::AssignText(value, StringView(text));
                    if (dropped > 0) {
                        ReportDropped(d, key, text.size() - dropped, text.size(), "bytes");
                    }
                    return true;
                }
            }

        }  // namespace Detail

        template <typename Alloc>
        struct DataAccessor<eastl::basic_string<char, Alloc>> {
            static void Save(ISerializer& s, const eastl::basic_string<char, Alloc>& value, StringView key) {
                Detail::SaveText(s, value, key);
            }

            static bool Load(IDeserializer& d, eastl::basic_string<char, Alloc>& value, StringView key) {
                return Detail::LoadText(d, value, key);
            }
        };

        // FixedString derives from a basic_string of another allocator, and a partial specialization does not match a derived class
        template <int N, bool Overflow, typename OverflowAlloc>
        struct DataAccessor<eastl::fixed_string<char, N, Overflow, OverflowAlloc>> {
            static void Save(ISerializer& s, const eastl::fixed_string<char, N, Overflow, OverflowAlloc>& value, StringView key) {
                Detail::SaveText(s, value, key);
            }

            static bool Load(IDeserializer& d, eastl::fixed_string<char, N, Overflow, OverflowAlloc>& value, StringView key) {
                return Detail::LoadText(d, value, key);
            }
        };

        // =================================================================
        // PoolString
        // =================================================================

        template <>
        struct DataAccessor<PoolString> {
            static void Save(ISerializer& s, const PoolString& value, eastl::string_view key) {
                s.WriteAttribute(key, value);
            }

            static bool Load(IDeserializer& d, PoolString& value, eastl::string_view key) {
                return d.ReadAttribute(key, value);
            }
        };

        // =================================================================
        // Reflected enums (via IsReflectedEnum concept)
        // =================================================================

        template <typename T>
        struct DataAccessor<T, std::enable_if_t<IsReflectedEnum<T>>> {
            static void Save(ISerializer& s, const T& value, eastl::string_view key) {
                auto sv = EnumUtils<T>::ToString(value);
                s.WriteAttribute(key, sv);
            }

            static bool Load(IDeserializer& d, T& value, eastl::string_view key) {
                String str;
                if (d.ReadAttribute(key, str)) {
                    auto result = EnumUtils<T>::Cast(StringView(str));
                    if (result) {
                        value = *result;
                        return true;
                    }
                    d.ReportError(key, eastl::string_view(Format("Invalid enum value: '{}'", str).c_str()));
                }
                return false;
            }
        };

        // =================================================================
        // Reflected objects (types with Serialize/Deserialize methods)
        // =================================================================

        template <typename T>
        struct DataAccessor<T, std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T> && !::BECore::Detail::IsIntrusivePtr<T> && ::BECore::Detail::HasSerialize<T, ISerializer> &&
                                                ::BECore::Detail::HasDeserialize<T, IDeserializer>>> {
            static void Save(ISerializer& s, const T& value, eastl::string_view key) {
                if (s.BeginObject(key)) {
                    value.Serialize(s);
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, T& value, eastl::string_view key) {
                if (d.BeginObject(key)) {
                    value.Deserialize(d);
                    d.EndObject();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // IntrusivePtr (non-factory)
        // =================================================================

        template <typename Ptr>
        struct DataAccessor<Ptr, std::enable_if_t<::BECore::Detail::IsIntrusivePtr<Ptr> && !::BECore::Detail::HasInnerFactory<Ptr>>> {
            using T = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;

            static void Save(ISerializer& s, const Ptr& ptr, eastl::string_view key) {
                if (ptr && s.BeginObject(key)) {
                    if constexpr (::BECore::Detail::HasSerialize<T, ISerializer>)
                        ptr->Serialize(s);
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, Ptr& ptr, eastl::string_view key) {
                if (d.BeginObject(key)) {
                    if (!ptr) {
                        if constexpr (!std::is_abstract_v<T>)
                            ptr = New<T>();
                    }
                    if (ptr) {
                        if constexpr (::BECore::Detail::HasDeserialize<T, IDeserializer>)
                            ptr->Deserialize(d);
                        if constexpr (::BECore::Detail::HasInitialize<T>)
                            ptr->Initialize();
                        // No Initialize(IDeserializer&) hook here: only the factory vector can drop a failed element (LoadElement).
                    }
                    d.EndObject();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // IntrusivePtr (factory-managed)
        // =================================================================

        template <typename Ptr>
        struct DataAccessor<Ptr, std::enable_if_t<::BECore::Detail::HasInnerFactory<Ptr>>> {
            using T = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;

            static void Save(ISerializer& s, const Ptr& ptr, eastl::string_view key) {
                if (ptr && s.BeginObject(key)) {
                    s.WriteAttribute("type", ptr->GetTypeMeta().typeName);
                    if constexpr (::BECore::Detail::HasSerialize<T, ISerializer>)
                        ptr->Serialize(s);
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, Ptr& ptr, eastl::string_view key) {
                if (d.BeginObject(key)) {
                    if (!ptr) {
                        String typeStr;
                        if (d.ReadAttribute("type", typeStr)) {
                            if (!Detail::FactoryTypeAllowed<T>(d, StringView(typeStr))) {
                                d.EndObject();
                                return true;
                            }
                            ptr = AbstractFactory<T>::GetInstance().Create(StringView(typeStr));
                            if (!ptr)
                                d.ReportError(key, eastl::string_view(Format("Unknown factory type: '{}'", typeStr).c_str()));
                        }
                        if (!ptr) {
                            if constexpr (!std::is_abstract_v<T>)
                                ptr = New<T>();
                        }
                    }
                    if (ptr) {
                        if constexpr (::BECore::Detail::HasDeserialize<T, IDeserializer>)
                            ptr->Deserialize(d);
                        if constexpr (::BECore::Detail::HasInitialize<T>)
                            ptr->Initialize();
                        // No Initialize(IDeserializer&) hook here: only the factory vector can drop a failed element (LoadElement).
                    }
                    d.EndObject();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // eastl::vector -- factory (polymorphic) case
        // (an element with `bool Initialize(IDeserializer&)` gets it called in its own scope;
        //  `false` drops the element and reports it, see Detail::LoadElement)
        // =================================================================

        template <typename Ptr, typename Alloc>
        struct DataAccessor<eastl::vector<Ptr, Alloc>, std::enable_if_t<::BECore::Detail::HasInnerFactory<Ptr>>> {
            static void Save(ISerializer& s, const eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
                Detail::SaveVectorLikeFactory(s, vec, key);
            }

            static bool Load(IDeserializer& d, eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
                return Detail::LoadVectorLikeFactory(d, vec, key);
            }
        };

        // =================================================================
        // eastl::vector -- general case
        // =================================================================

        template <typename Elem, typename Alloc>
        struct DataAccessor<eastl::vector<Elem, Alloc>, std::enable_if_t<!::BECore::Detail::HasInnerFactory<Elem>>> {
            static void Save(ISerializer& s, const eastl::vector<Elem, Alloc>& vec, eastl::string_view key) {
                Detail::SaveVectorLike(s, vec, key);
            }

            static bool Load(IDeserializer& d, eastl::vector<Elem, Alloc>& vec, eastl::string_view key) {
                return Detail::LoadVectorLike(d, vec, key);
            }
        };

        // =================================================================
        // eastl::fixed_vector -- factory (polymorphic) case
        // =================================================================

        template <typename Ptr, size_t N, bool bOverflow, typename Alloc>
        struct DataAccessor<eastl::fixed_vector<Ptr, N, bOverflow, Alloc>, std::enable_if_t<::BECore::Detail::HasInnerFactory<Ptr>>> {
            static void Save(ISerializer& s, const eastl::fixed_vector<Ptr, N, bOverflow, Alloc>& vec, eastl::string_view key) {
                Detail::SaveVectorLikeFactory(s, vec, key);
            }

            static bool Load(IDeserializer& d, eastl::fixed_vector<Ptr, N, bOverflow, Alloc>& vec, eastl::string_view key) {
                return Detail::LoadVectorLikeFactory(d, vec, key);
            }
        };

        // =================================================================
        // eastl::fixed_vector -- general case
        // =================================================================

        template <typename Elem, size_t N, bool bOverflow, typename Alloc>
        struct DataAccessor<eastl::fixed_vector<Elem, N, bOverflow, Alloc>, std::enable_if_t<!::BECore::Detail::HasInnerFactory<Elem>>> {
            static void Save(ISerializer& s, const eastl::fixed_vector<Elem, N, bOverflow, Alloc>& vec, eastl::string_view key) {
                Detail::SaveVectorLike(s, vec, key);
            }

            static bool Load(IDeserializer& d, eastl::fixed_vector<Elem, N, bOverflow, Alloc>& vec, eastl::string_view key) {
                return Detail::LoadVectorLike(d, vec, key);
            }
        };

        // =================================================================
        // eastl::array<T, N>
        // =================================================================

        template <typename T, size_t N>
        struct DataAccessor<eastl::array<T, N>> {
            static void Save(ISerializer& s, const eastl::array<T, N>& arr, eastl::string_view key) {
                Detail::SaveFixedArray(s, arr, key);
            }

            static bool Load(IDeserializer& d, eastl::array<T, N>& arr, eastl::string_view key) {
                return Detail::LoadFixedArray(d, arr, key);
            }
        };

        // =================================================================
        // eastl::pair<A, B>
        // =================================================================

        template <typename A, typename B>
        struct DataAccessor<eastl::pair<A, B>> {
            static void Save(ISerializer& s, const eastl::pair<A, B>& p, eastl::string_view key) {
                if (s.BeginObject(key)) {
                    DataAccessor<A>::Save(s, p.first, "first");
                    DataAccessor<B>::Save(s, p.second, "second");
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, eastl::pair<A, B>& p, eastl::string_view key) {
                if (d.BeginObject(key)) {
                    DataAccessor<A>::Load(d, p.first, "first");
                    DataAccessor<B>::Load(d, p.second, "second");
                    d.EndObject();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // std::optional<T>
        // =================================================================

        template <typename T>
        struct DataAccessor<std::optional<T>> {
            static void Save(ISerializer& s, const std::optional<T>& opt, eastl::string_view key) {
                if (opt.has_value())
                    DataAccessor<T>::Save(s, *opt, key);
            }

            static bool Load(IDeserializer& d, std::optional<T>& opt, eastl::string_view key) {
                T tmp{};
                if (DataAccessor<T>::Load(d, tmp, key)) {
                    opt = eastl::move(tmp);
                    return true;
                }
                opt.reset();
                return false;
            }
        };

        // =================================================================
        // std::variant<Ts...> (Variant) and std::monostate
        // =================================================================

        /**
         * @brief A Variant is an object `<key index="i">` holding the active alternative under `value` (an attribute for a scalar, a
         *        child for an object or an array); as an element of an array or a map the same two names sit in the element itself
         *
         * A missing or out-of-range index is an error at `<key>.index` and leaves the variant as it was. A valid index emplaces a
         * default alternative of that index before reading it, so a load whose `value` is missing or bad keeps that new default.
         */
        template <typename... Ts>
        struct DataAccessor<std::variant<Ts...>> {
            static void Save(ISerializer& s, const std::variant<Ts...>& value, StringView key) {
                if (s.BeginObject(key)) {
                    Detail::SaveVariantBody(s, value);
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, std::variant<Ts...>& value, StringView key) {
                if (!d.BeginObject(key)) {
                    return false;
                }
                const bool loaded = Detail::LoadVariantBody(d, value);
                d.EndObject();
                return loaded;
            }
        };

        /// The empty alternative of a Variant: its index alone says it is active
        template <>
        struct DataAccessor<std::monostate> {
            static void Save(ISerializer& /*s*/, const std::monostate& /*value*/, StringView /*key*/) {}

            static bool Load(IDeserializer& /*d*/, std::monostate& /*value*/, StringView /*key*/) {
                return true;
            }
        };

        // =================================================================
        // eastl::unordered_map (HashMap) and eastl::fixed_hash_map (FixedHashMap) -- with sorted-key Save
        // =================================================================

        namespace Detail {

            /// One entry of a hash map in the sorted order of Save. Its own swap: with a std:: type in K or V (a Variant value), an
            /// eastl::pair of the two pointers would find both eastl::swap and std::swap in the sort and not compile
            template <typename K, typename V>
            struct SortedEntry {
                const K* key = nullptr;
                const V* value = nullptr;

                friend void swap(SortedEntry& a, SortedEntry& b) noexcept {
                    const SortedEntry held = a;
                    a = b;
                    b = held;
                }
            };

            template <typename HashMapType>
            void SaveHashMapLike(ISerializer& s, const HashMapType& map, StringView key) {
                using K = typename HashMapType::key_type;
                using V = typename HashMapType::mapped_type;
                size_t count = map.size();
                if (s.BeginArray(key, "entry", count)) {
                    if constexpr (requires(const K& a, const K& b) { {a < b}->std::convertible_to<bool>; }) {
                        Vector<SortedEntry<K, V>> sorted;
                        sorted.reserve(map.size());
                        for (const auto& [k, v] : map) {
                            sorted.push_back({&k, &v});
                        }
                        eastl::sort(sorted.begin(), sorted.end(), [](const SortedEntry<K, V>& a, const SortedEntry<K, V>& b) { return *a.key < *b.key; });
                        for (const SortedEntry<K, V>& entry : sorted) {
                            if (s.BeginArrayElement()) {
                                s.WriteAttribute("key", *entry.key);
                                Detail::SaveElement(s, *entry.value);
                                s.EndArrayElement();
                            }
                        }
                    } else {
                        for (const auto& [k, v] : map) {
                            if (s.BeginArrayElement()) {
                                s.WriteAttribute("key", k);
                                Detail::SaveElement(s, v);
                                s.EndArrayElement();
                            }
                        }
                    }
                    s.EndArray();
                }
            }

            /// @p FixedStorage: the nodes are in the map's own block, so nothing is reserved, and a map that may not overflow keeps what fits and
            /// reports the rest
            template <bool FixedStorage, typename HashMapType>
            bool LoadHashMapLike(IDeserializer& d, HashMapType& map, StringView key) {
                using K = typename HashMapType::key_type;
                using V = typename HashMapType::mapped_type;
                size_t count = 0;
                if (d.BeginArray(key, "entry", count)) {
                    map.clear();
                    if constexpr (!FixedStorage) {
                        map.reserve(count);
                    }
                    size_t offered = 0;
                    size_t dropped = 0;
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            ++offered;
                            K k{};
                            d.ReadAttribute("key", k);
                            V element = MakeElement<V>(d);
                            if (IsFull(map)) {
                                ++dropped;
                            } else {
                                map.emplace(eastl::move(k), eastl::move(element));
                            }
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    if (dropped > 0) {
                        ReportDropped(d, key, offered - dropped, offered, "entries");
                    }
                    return true;
                }
                return false;
            }

        }  // namespace Detail

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc>
        struct DataAccessor<eastl::unordered_map<K, V, Hash, Pred, Alloc>> {
            static void Save(ISerializer& s, const eastl::unordered_map<K, V, Hash, Pred, Alloc>& map, StringView key) {
                Detail::SaveHashMapLike(s, map, key);
            }

            static bool Load(IDeserializer& d, eastl::unordered_map<K, V, Hash, Pred, Alloc>& map, StringView key) {
                return Detail::LoadHashMapLike<false>(d, map, key);
            }
        };

        // FixedHashMap derives from a hash_map of another allocator: its own specialization
        template <typename K, typename V, size_t NodeCount, size_t BucketCount, bool Overflow, typename Hash, typename Pred, bool CacheHashCode, typename OverflowAlloc>
        struct DataAccessor<eastl::fixed_hash_map<K, V, NodeCount, BucketCount, Overflow, Hash, Pred, CacheHashCode, OverflowAlloc>> {
            using Map = eastl::fixed_hash_map<K, V, NodeCount, BucketCount, Overflow, Hash, Pred, CacheHashCode, OverflowAlloc>;

            static void Save(ISerializer& s, const Map& map, StringView key) {
                Detail::SaveHashMapLike(s, map, key);
            }

            static bool Load(IDeserializer& d, Map& map, StringView key) {
                return Detail::LoadHashMapLike<true>(d, map, key);
            }
        };

        // =================================================================
        // eastl::map<K, V, Cmp, Alloc>
        // =================================================================

        template <typename K, typename V, typename Cmp, typename Alloc>
        struct DataAccessor<eastl::map<K, V, Cmp, Alloc>> {
            static void Save(ISerializer& s, const eastl::map<K, V, Cmp, Alloc>& map, eastl::string_view key) {
                size_t count = map.size();
                if (s.BeginArray(key, "entry", count)) {
                    for (const auto& [k, v] : map) {
                        if (s.BeginArrayElement()) {
                            s.WriteAttribute("key", k);
                            Detail::SaveElement(s, v);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            static bool Load(IDeserializer& d, eastl::map<K, V, Cmp, Alloc>& map, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, "entry", count)) {
                    map.clear();
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            K k{};
                            d.ReadAttribute("key", k);
                            map.emplace(eastl::move(k), Detail::MakeElement<V>(d));
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // Ser::Save / Ser::Load wrappers (with default-skipping support)
        // =================================================================

        template <typename T>
        void Save(ISerializer& s, const T& value, eastl::string_view key) {
            static_assert(HasDataAccessor<T>, "The type has no Ser::DataAccessor: it is not a reflected type (see Reflection/DataAccessor.h)");
            if (s.IsSkipDefaults() && !DefaultChecker<T>::AlwaysFalse && DefaultChecker<T>::IsDefault(value))
                return;
            DataAccessor<T>::Save(s, value, key);
        }

        template <typename T>
        bool Load(IDeserializer& d, T& value, eastl::string_view key) {
            static_assert(HasDataAccessor<T>, "The type has no Ser::DataAccessor: it is not a reflected type (see Reflection/DataAccessor.h)");
            return DataAccessor<T>::Load(d, value, key);
        }

        namespace Detail {

            /// A number as the largest type of its kind, so that a finding reads it as the number it is (an int8_t is not a character)
            template <typename T>
            auto AsReportedNumber(T value) {
                if constexpr (std::is_floating_point_v<T>) {
                    return static_cast<double>(value);
                } else if constexpr (std::is_signed_v<T>) {
                    return static_cast<int64_t>(value);
                } else {
                    return static_cast<uint64_t>(value);
                }
            }

            template <typename T>
            struct IsOptionalType : std::false_type {};

            template <typename T>
            struct IsOptionalType<std::optional<T>> : std::true_type {};

            /// A map: its mapped values are checked, the keys are not
            template <typename T>
            concept IsRangeMap = requires(const T& map) {
                typename T::mapped_type;
                map.begin()->second;
            };

            /// A vector-like or an array; a string is text, not a sequence of numbers
            template <typename T>
            concept IsRangeSequence = !IsRangeMap<T> && requires(const T& sequence) {
                sequence.begin();
                sequence.end();
                requires(!std::is_same_v<typename T::value_type, char>);
            };

            /// Where a checked number sits in its field: the key, then `[i]` per element and `.value` per variant; text only for a finding
            struct RangeSite {
                const RangeSite* parent = nullptr;
                StringView name;
                size_t index = 0;
                bool isIndex = false;
            };

            inline void AppendRangeSite(String& out, const RangeSite& site) {
                if (site.parent != nullptr) {
                    AppendRangeSite(out, *site.parent);
                }
                if (site.isIndex) {
                    out += Format("[{}]", site.index);
                } else {
                    if (site.parent != nullptr) {
                        out += '.';
                    }
                    out.append(site.name.data(), site.name.size());
                }
            }

            /// Reports every number of @p value outside the range of @p attributes, walking containers, an engaged optional and the active alternative of a variant
            template <typename T>
            void CheckRange(IDeserializer& d, const T& value, const RangeSite& site, const FieldAttributes& attributes) {
                if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>) {
                    const double number = static_cast<double>(value);
                    if (!(number >= attributes.rangeMin && number <= attributes.rangeMax)) {
                        const String message = Format("Value {} is outside the range [{}, {}]", AsReportedNumber(value), attributes.rangeMin, attributes.rangeMax);
                        if (site.parent == nullptr) {
                            d.ReportFinding(site.name, message);
                        } else {
                            String path;
                            AppendRangeSite(path, site);
                            d.ReportFinding(path, message);
                        }
                    }
                } else if constexpr (IsVariant<T>) {
                    const RangeSite alternative{&site, kVariantValueKey};
                    std::visit([&](const auto& active) { CheckRange(d, active, alternative, attributes); }, value);
                } else if constexpr (IsOptionalType<T>::value) {
                    if (value.has_value()) {
                        CheckRange(d, *value, site, attributes);
                    }
                } else if constexpr (IsRangeMap<T>) {
                    size_t position = 0;
                    for (const auto& entry : value) {
                        CheckRange(d, entry.second, RangeSite{&site, {}, position++, true}, attributes);
                    }
                } else if constexpr (IsRangeSequence<T>) {
                    size_t position = 0;
                    for (const auto& element : value) {
                        CheckRange(d, element, RangeSite{&site, {}, position++, true}, attributes);
                    }
                }
            }

        }  // namespace Detail

        /**
         * @brief Load a field that has attributes: the plain Load, then the checks of `required` and `range`
         *
         * The checks run only when d.GetDiagnostics() is not Off, and only report (IDeserializer::ReportFinding): the field keeps
         * what was read, and the result is that of the plain Load. `required` reports a key the scope does not have (IDeserializer::HasKey);
         * `range` reports a number that was read and lies outside [rangeMin, rangeMax], a NaN included. bool is not a number here.
         * `range` reaches the numeric elements of a container (at `key[i]`, `i` the position in the container's own iteration, which for a
         * hash map is no sorted order), the mapped values of a map, an engaged optional and a variant's active alternative (`key.value`);
         * objects, pairs and strings are not walked.
         */
        template <typename T>
        bool Load(IDeserializer& d, T& value, eastl::string_view key, const FieldAttributes& attributes) {
            const bool loaded = Load(d, value, key);
            if (d.GetDiagnostics() == DiagnosticsMode::Off) {
                return loaded;
            }
            if (attributes.required && !d.HasKey(key)) {
                d.ReportFinding(key, "Required field is missing");
            }
            if (loaded && attributes.hasRange) {
                Detail::CheckRange(d, value, Detail::RangeSite{nullptr, key}, attributes);
            }
            return loaded;
        }

    }  // namespace Ser

}  // namespace BECore
