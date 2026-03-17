#pragma once

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/FactoryTraits.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <EASTL/array.h>
#include <EASTL/map.h>
#include <EASTL/optional.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

namespace BECore {

    // =========================================================================
    // Internal helpers (shared with FieldSerializer, migrated here)
    // =========================================================================

    namespace Detail {

        template <typename Ptr>
        struct IntrusivePtrPointeeType {};

        template <typename T>
        struct IntrusivePtrPointeeType<IntrusivePtrAtomic<T>> {
            using type = T;
        };

        template <typename T>
        struct IntrusivePtrPointeeType<IntrusivePtrNonAtomic<T>> {
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
    // DataAccessor<T> -- primary template (static_assert for unhandled types)
    // =========================================================================

    namespace Ser {

        template <typename T, typename = void>
        struct DataAccessor {
            static_assert(sizeof(T) != sizeof(T), "No DataAccessor specialization for this type");
        };

        // =================================================================
        // Internal element helpers (used by vector/map specializations)
        // =================================================================

        namespace Detail {

            template <typename T>
            void SaveElement(ISerializer& s, const T& item) {
                if constexpr (::BECore::Detail::IsIntrusivePtr<T>) {
                    if (item) {
                        using Pointee = typename ::BECore::Detail::IntrusivePtrPointeeType<T>::type;
                        if constexpr (HasFactory<Pointee>)
                            s.WriteAttribute("type", item->GetTypeMeta().typeName);
                        if constexpr (::BECore::Detail::HasSerialize<Pointee, ISerializer>)
                            item->Serialize(s);
                    }
                } else if constexpr (::BECore::Detail::HasSerialize<T, ISerializer>) {
                    item.Serialize(s);
                } else {
                    s.WriteAttribute("value", item);
                }
            }

            template <typename T>
            void LoadElement(IDeserializer& d, T& item) {
                if constexpr (::BECore::Detail::IsIntrusivePtr<T>) {
                    using Pointee = typename ::BECore::Detail::IntrusivePtrPointeeType<T>::type;
                    if (!item) {
                        eastl::string typeStr;
                        if (d.ReadAttribute("type", typeStr))
                            item = AbstractFactory<Pointee>::GetInstance().Create(eastl::string_view(typeStr.data(), typeStr.size()));
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
                    }
                } else if constexpr (::BECore::Detail::HasDeserialize<T, IDeserializer>) {
                    item.Deserialize(d);
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
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            using Elem = typename Container::value_type;
                            vec.push_back(MakeElement<Elem>(d));
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
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
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            bool enabled = true;
                            d.ReadAttribute("enabled", enabled);
                            if (enabled) {
                                Ptr item;
                                LoadElement(d, item);
                                if (item)
                                    vec.push_back(eastl::move(item));
                            }
                            d.EndArrayElement();
                        }
                    }
                    d.EndArray();
                    return true;
                }
                return false;
            }

            // -----------------------------------------------------------------
            // Fixed-size array helpers (eastl::array, std::array)
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
                    // Reset remaining elements to default if XML had fewer elements
                    using Elem = typename Arr::value_type;
                    for (size_t i = loadCount; i < arr.size(); ++i)
                        DefaultMaker<Elem>::MakeDefault(arr[i]);
                    // Drain any extra elements from the XML
                    for (size_t i = loadCount; i < count; ++i) {
                        if (d.BeginArrayElement())
                            d.EndArrayElement();
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
        // eastl::string
        // =================================================================

        template <>
        struct DataAccessor<eastl::string> {
            static void Save(ISerializer& s, const eastl::string& value, eastl::string_view key) {
                s.WriteAttribute(key, eastl::string_view(value.data(), value.size()));
            }

            static bool Load(IDeserializer& d, eastl::string& value, eastl::string_view key) {
                return d.ReadAttribute(key, value);
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
                eastl::string str;
                if (d.ReadAttribute(key, str)) {
                    auto result = EnumUtils<T>::Cast(eastl::string_view(str.data(), str.size()));
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
                        eastl::string typeStr;
                        if (d.ReadAttribute("type", typeStr)) {
                            ptr = AbstractFactory<T>::GetInstance().Create(eastl::string_view(typeStr.data(), typeStr.size()));
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
                    }
                    d.EndObject();
                    return true;
                }
                return false;
            }
        };

        // =================================================================
        // eastl::vector -- factory (polymorphic) case
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
        // std::array<T, N>
        // =================================================================

        template <typename T, size_t N>
        struct DataAccessor<std::array<T, N>> {
            static void Save(ISerializer& s, const std::array<T, N>& arr, eastl::string_view key) {
                Detail::SaveFixedArray(s, arr, key);
            }

            static bool Load(IDeserializer& d, std::array<T, N>& arr, eastl::string_view key) {
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
        // std::pair<A, B>
        // =================================================================

        template <typename A, typename B>
        struct DataAccessor<std::pair<A, B>> {
            static void Save(ISerializer& s, const std::pair<A, B>& p, eastl::string_view key) {
                if (s.BeginObject(key)) {
                    DataAccessor<A>::Save(s, p.first, "first");
                    DataAccessor<B>::Save(s, p.second, "second");
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, std::pair<A, B>& p, eastl::string_view key) {
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
        // eastl::optional<T>
        // =================================================================

        template <typename T>
        struct DataAccessor<eastl::optional<T>> {
            static void Save(ISerializer& s, const eastl::optional<T>& opt, eastl::string_view key) {
                if (opt.has_value())
                    DataAccessor<T>::Save(s, *opt, key);
            }

            static bool Load(IDeserializer& d, eastl::optional<T>& opt, eastl::string_view key) {
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
        // eastl::unordered_map -- with sorted-key Save
        // =================================================================

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc>
        struct DataAccessor<eastl::unordered_map<K, V, Hash, Pred, Alloc>> {
            static void Save(ISerializer& s, const eastl::unordered_map<K, V, Hash, Pred, Alloc>& map, eastl::string_view key) {
                size_t count = map.size();
                if (s.BeginArray(key, "entry", count)) {
                    if constexpr (requires(const K& a, const K& b) {
                                      {
                                          a < b
                                      } -> std::convertible_to<bool>;
                                  }) {
                        eastl::vector<eastl::pair<const K*, const V*>> sorted;
                        sorted.reserve(map.size());
                        for (const auto& [k, v] : map)
                            sorted.push_back({&k, &v});
                        eastl::sort(sorted.begin(), sorted.end(), [](const eastl::pair<const K*, const V*>& a, const eastl::pair<const K*, const V*>& b) { return *a.first < *b.first; });
                        for (const auto& [kPtr, vPtr] : sorted) {
                            if (s.BeginArrayElement()) {
                                s.WriteAttribute("key", *kPtr);
                                Detail::SaveElement(s, *vPtr);
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

            static bool Load(IDeserializer& d, eastl::unordered_map<K, V, Hash, Pred, Alloc>& map, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, "entry", count)) {
                    map.clear();
                    map.reserve(count);
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
            if (s.IsSkipDefaults() && !DefaultChecker<T>::AlwaysFalse && DefaultChecker<T>::IsDefault(value))
                return;
            DataAccessor<T>::Save(s, value, key);
        }

        template <typename T>
        bool Load(IDeserializer& d, T& value, eastl::string_view key) {
            return DataAccessor<T>::Load(d, value, key);
        }

    }  // namespace Ser

}  // namespace BECore
