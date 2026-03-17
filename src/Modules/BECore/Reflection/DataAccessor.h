#pragma once

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/FactoryTraits.h>
#include <BECore/Reflection/ISerializer.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/SerializationTraits.h>
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
        concept HasInitialize = requires(T& t) {
            t.Initialize();
        };

        template <typename T, typename Archive>
        concept HasSerialize = requires(const T& t, Archive& a) {
            t.Serialize(a);
        };

        template <typename T, typename Archive>
        concept HasDeserialize = requires(T& t, Archive& a) {
            t.Deserialize(a);
        };

    }  // namespace Detail

    // =========================================================================
    // DataAccessor<T> -- primary template (static_assert for unhandled types)
    // =========================================================================

    namespace Ser {

        template <typename T, typename = void>
        struct DataAccessor {
            static_assert(sizeof(T) != sizeof(T),
                          "No DataAccessor specialization for this type");
        };

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
                }
                return false;
            }
        };

        // =================================================================
        // Reflected objects (types with Serialize/Deserialize methods)
        // =================================================================

        template <typename T>
        struct DataAccessor<T, std::enable_if_t<!std::is_arithmetic_v<T> && !std::is_enum_v<T>
                                                && !Detail::IsIntrusivePtr<T>
                                                && Detail::HasSerialize<T, ISerializer>
                                                && Detail::HasDeserialize<T, IDeserializer>>> {
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
        struct DataAccessor<Ptr, std::enable_if_t<Detail::IsIntrusivePtr<Ptr> && !Detail::HasInnerFactory<Ptr>>> {
            using T = typename Detail::IntrusivePtrPointeeType<Ptr>::type;

            static void Save(ISerializer& s, const Ptr& ptr, eastl::string_view key) {
                if (ptr && s.BeginObject(key)) {
                    if constexpr (Detail::HasSerialize<T, ISerializer>)
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
                        if constexpr (Detail::HasDeserialize<T, IDeserializer>)
                            ptr->Deserialize(d);
                        if constexpr (Detail::HasInitialize<T>)
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
        struct DataAccessor<Ptr, std::enable_if_t<Detail::HasInnerFactory<Ptr>>> {
            using T = typename Detail::IntrusivePtrPointeeType<Ptr>::type;

            static void Save(ISerializer& s, const Ptr& ptr, eastl::string_view key) {
                if (ptr && s.BeginObject(key)) {
                    s.WriteAttribute("type", ptr->GetTypeMeta().typeName);
                    if constexpr (Detail::HasSerialize<T, ISerializer>)
                        ptr->Serialize(s);
                    s.EndObject();
                }
            }

            static bool Load(IDeserializer& d, Ptr& ptr, eastl::string_view key) {
                if (d.BeginObject(key)) {
                    if (!ptr) {
                        eastl::string typeStr;
                        if (d.ReadAttribute("type", typeStr))
                            ptr = AbstractFactory<T>::GetInstance().Create(
                                eastl::string_view(typeStr.data(), typeStr.size()));
                        if (!ptr) {
                            if constexpr (!std::is_abstract_v<T>)
                                ptr = New<T>();
                        }
                    }
                    if (ptr) {
                        if constexpr (Detail::HasDeserialize<T, IDeserializer>)
                            ptr->Deserialize(d);
                        if constexpr (Detail::HasInitialize<T>)
                            ptr->Initialize();
                    }
                    d.EndObject();
                    return true;
                }
                return false;
            }
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
                            item = AbstractFactory<Pointee>::GetInstance().Create(
                                eastl::string_view(typeStr.data(), typeStr.size()));
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

        }  // namespace Detail

        // =================================================================
        // eastl::vector -- factory (polymorphic) case
        // =================================================================

        template <typename Ptr, typename Alloc>
        struct DataAccessor<eastl::vector<Ptr, Alloc>,
                            std::enable_if_t<::BECore::Detail::HasInnerFactory<Ptr>>> {
            using Base = typename ::BECore::Detail::IntrusivePtrPointeeType<Ptr>::type;
            using Traits = FactoryTraits<Base>;

            static void Save(ISerializer& s, const eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
                size_t count = vec.size();
                if (s.BeginArray(key, Traits::elementName, count)) {
                    for (const auto& item : vec) {
                        if (item && s.BeginArrayElement()) {
                            s.WriteAttribute("enabled", true);
                            Detail::SaveElement(s, item);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            static bool Load(IDeserializer& d, eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, Traits::elementName, count)) {
                    vec.clear();
                    vec.reserve(count);
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            bool enabled = true;
                            d.ReadAttribute("enabled", enabled);
                            if (enabled) {
                                Ptr item;
                                Detail::LoadElement(d, item);
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
        };

        // =================================================================
        // eastl::vector -- general case
        // =================================================================

        template <typename Elem, typename Alloc>
        struct DataAccessor<eastl::vector<Elem, Alloc>,
                            std::enable_if_t<!::BECore::Detail::HasInnerFactory<Elem>>> {
            static void Save(ISerializer& s, const eastl::vector<Elem, Alloc>& vec, eastl::string_view key) {
                size_t count = vec.size();
                if (s.BeginArray(key, "item", count)) {
                    for (const auto& item : vec) {
                        if (s.BeginArrayElement()) {
                            Detail::SaveElement(s, item);
                            s.EndArrayElement();
                        }
                    }
                    s.EndArray();
                }
            }

            static bool Load(IDeserializer& d, eastl::vector<Elem, Alloc>& vec, eastl::string_view key) {
                size_t count = 0;
                if (d.BeginArray(key, "item", count)) {
                    vec.clear();
                    vec.reserve(count);
                    for (size_t i = 0; i < count; ++i) {
                        if (d.BeginArrayElement()) {
                            vec.push_back(Detail::MakeElement<Elem>(d));
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
        // eastl::unordered_map
        // =================================================================

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc>
        struct DataAccessor<eastl::unordered_map<K, V, Hash, Pred, Alloc>> {
            static void Save(ISerializer& s, const eastl::unordered_map<K, V, Hash, Pred, Alloc>& map,
                             eastl::string_view key) {
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

            static bool Load(IDeserializer& d, eastl::unordered_map<K, V, Hash, Pred, Alloc>& map,
                             eastl::string_view key) {
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
