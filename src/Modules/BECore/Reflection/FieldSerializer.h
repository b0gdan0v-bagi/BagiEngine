#pragma once

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/FactoryTraits.h>
#include <BECore/Reflection/ISerializer.h>
#include <EASTL/unordered_map.h>

namespace BECore {

    // =========================================================================
    // Internal helpers
    // =========================================================================

    namespace Detail {

        /**
         * @brief Trait to extract the factory base type from an intrusive smart pointer,
         *        but only when that type has a FactoryTraits specialization.
         *
         * Used by HasInnerFactory to detect polymorphic factory containers.
         */
        template <typename Ptr>
        struct InnerFactoryType {};

        template <typename T>
        requires HasFactory<T> struct InnerFactoryType<IntrusivePtrAtomic<T>> {
            using type = T;
        };

        template <typename T>
        requires HasFactory<T> struct InnerFactoryType<IntrusivePtrNonAtomic<T>> {
            using type = T;
        };

        template <typename Ptr>
        concept HasInnerFactory = requires {
            typename InnerFactoryType<Ptr>::type;
        };

        /**
         * @brief Trait to extract the pointee type from any IntrusivePtr (for New + Deserialize path).
         */
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

        /**
         * @brief Concept that matches types with an Initialize() method (optional post-Deserialize step).
         */
        template <typename T>
        concept HasInitialize = requires(T& t) {
            t.Initialize();
        };

        /**
         * @brief Concept that matches types with a Serialize(Archive) const method.
         */
        template <typename T, typename Archive>
        concept HasSerialize = requires(const T& t, Archive& a) {
            t.Serialize(a);
        };

        /**
         * @brief Concept that matches types with a Deserialize(Archive) method.
         */
        template <typename T, typename Archive>
        concept HasDeserialize = requires(T & t, Archive& a) {
            t.Deserialize(a);
        };

        /**
         * @brief Serialize a single element value to the current archive node.
         *
         * Dispatches to Serialize() for reflected objects, or WriteAttribute("value", ...)
         * for primitives. Smart pointers dereference and dispatch.
         */
        template <typename Archive, typename T>
        void SerializeElement(Archive& s, const T& item) {
            if constexpr (HasSerialize<T, Archive>) {
                item.Serialize(s);
            } else if constexpr (requires { (*item).Serialize(s); }) {
                if (item) {
                    (*item).Serialize(s);
                }
            } else {
                s.WriteAttribute("value", item);
            }
        }

        /**
         * @brief Deserialize a single element value from the current archive node.
         *
         * For IntrusivePtr elements (e.g. in vector/map): creates via New, Deserialize, optional Initialize.
         */
        template <typename Archive, typename T>
        void DeserializeElement(Archive& d, T& item) {
            if constexpr (IsIntrusivePtr<T>) {
                using U = typename IntrusivePtrPointeeType<T>::type;
                if constexpr (HasDeserialize<U, Archive> && !std::is_abstract_v<U>) {
                    item = New<U>();
                    item->Deserialize(d);
                    if constexpr (HasInitialize<U>) {
                        item->Initialize();
                    }
                } else if constexpr (requires { (*item).Deserialize(d); }) {
                    if (item) {
                        (*item).Deserialize(d);
                    }
                } else {
                    d.ReadAttribute("value", item);
                }
            } else if constexpr (HasDeserialize<T, Archive>) {
                item.Deserialize(d);
            } else if constexpr (requires { (*item).Deserialize(d); }) {
                if (item) {
                    (*item).Deserialize(d);
                }
            } else {
                d.ReadAttribute("value", item);
            }
        }

    }  // namespace Detail

    // =========================================================================
    // Reflected objects (anything with Serialize/Deserialize)
    // =========================================================================

    /**
     * @brief Serialize a reflected object as a named XML child element.
     *
     * Wraps the object in BeginObject/EndObject and delegates to its Serialize().
     */
    template <typename Archive, typename T>
    requires Detail::HasSerialize<T, Archive> void SerializeField(Archive& s, eastl::string_view name, const T& value) {
        if (s.BeginObject(name)) {
            value.Serialize(s);
            s.EndObject();
        }
    }

    /**
     * @brief Deserialize a reflected object from a named XML child element.
     */
    template <typename Archive, typename T>
    requires Detail::HasDeserialize<T, Archive> void DeserializeField(Archive& d, eastl::string_view name, T& value) {
        if (d.BeginObject(name)) {
            value.Deserialize(d);
            d.EndObject();
        }
    }

    // =========================================================================
    // Smart pointers (IntrusivePtrAtomic / IntrusivePtrNonAtomic)
    // =========================================================================

    /**
     * @brief Serialize an IntrusivePtrAtomic<T> as a named child element.
     *
     * No-op if the pointer is null. Delegates to SerializeField<T>.
     */
    template <typename Archive, typename T>
    void SerializeField(Archive& s, eastl::string_view name, const IntrusivePtrAtomic<T>& ptr) {
        if (ptr) {
            SerializeField(s, name, *ptr);
        }
    }

    /**
     * @brief Deserialize into an existing IntrusivePtrAtomic<T> from a named child element.
     *
     * The pointer must already be non-null; typically used for single owned objects.
     */
    template <typename Archive, typename T>
    void DeserializeField(Archive& d, eastl::string_view name, IntrusivePtrAtomic<T>& ptr) {
        if (ptr) {
            DeserializeField(d, name, *ptr);
        }
    }

    template <typename Archive, typename T>
    void SerializeField(Archive& s, eastl::string_view name, const IntrusivePtrNonAtomic<T>& ptr) {
        if (ptr) {
            SerializeField(s, name, *ptr);
        }
    }

    template <typename Archive, typename T>
    void DeserializeField(Archive& d, eastl::string_view name, IntrusivePtrNonAtomic<T>& ptr) {
        if (ptr) {
            DeserializeField(d, name, *ptr);
        }
    }

    // =========================================================================
    // eastl::vector — polymorphic factory case (more constrained, selected first)
    // =========================================================================

    /**
     * @brief Serialize a vector of factory-managed polymorphic pointers.
     *
     * Writes the concrete class name as the `type` attribute and `enabled="true"` per element.
     * XML: <fieldName><elementName type="ConcreteClassName" enabled="true" .../></fieldName>
     */
    template <typename Archive, typename Ptr, typename Alloc>
    requires Detail::HasInnerFactory<Ptr> void SerializeField(Archive& s, eastl::string_view name, const eastl::vector<Ptr, Alloc>& vec) {
        using Base = typename Detail::InnerFactoryType<Ptr>::type;
        using Traits = FactoryTraits<Base>;

        size_t count = vec.size();
        if (s.BeginArray(name, Traits::elementName, count)) {
            for (const auto& item : vec) {
                if (item && s.BeginArrayElement()) {
                    s.WriteAttribute("type", item->GetTypeMeta().typeName);
                    s.WriteAttribute("enabled", true);
                    item->Serialize(s);
                    s.EndArrayElement();
                }
            }
            s.EndArray();
        }
    }

    /**
     * @brief Deserialize a vector of factory-managed polymorphic pointers.
     *
     * Reads the `type` attribute (full class name) and optional `enabled` attribute per element.
     * Creates instances via AbstractFactory<Base> by class name — no derived-class headers needed.
     * Disabled elements (enabled="false") are skipped.
     */
    template <typename Archive, typename Ptr, typename Alloc>
    requires Detail::HasInnerFactory<Ptr> void DeserializeField(Archive& d, eastl::string_view name, eastl::vector<Ptr, Alloc>& vec) {
        using Base = typename Detail::InnerFactoryType<Ptr>::type;
        using Traits = FactoryTraits<Base>;

        size_t count = 0;
        if (d.BeginArray(name, Traits::elementName, count)) {
            vec.clear();
            vec.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (d.BeginArrayElement()) {
                    bool enabled = true;
                    d.ReadAttribute("enabled", enabled);
                    if (enabled) {
                        eastl::string typeStr;
                        if (d.ReadAttribute("type", typeStr)) {
                            auto item = AbstractFactory<Base>::GetInstance().Create(
                                eastl::string_view(typeStr.data(), typeStr.size()));
                            if (item) {
                                item->Deserialize(d);
                                if constexpr (Detail::HasInitialize<Base>) {
                                    item->Initialize();
                                }
                                vec.push_back(eastl::move(item));
                            }
                        }
                    }
                    d.EndArrayElement();
                }
            }
            d.EndArray();
        }
    }

    // =========================================================================
    // eastl::vector — general case
    // =========================================================================

    /**
     * @brief Serialize a vector of arbitrary elements.
     *
     * Each element is serialized into an `<item>` child element.
     * Primitives use a `value` attribute; reflected objects use Serialize().
     */
    template <typename Archive, typename Elem, typename Alloc>
    void SerializeField(Archive& s, eastl::string_view name, const eastl::vector<Elem, Alloc>& vec) {
        size_t count = vec.size();
        if (s.BeginArray(name, "item", count)) {
            for (const auto& item : vec) {
                if (s.BeginArrayElement()) {
                    Detail::SerializeElement(s, item);
                    s.EndArrayElement();
                }
            }
            s.EndArray();
        }
    }

    /**
     * @brief Deserialize a vector of arbitrary elements.
     *
     * Each `<item>` element is deserialized in order.
     * Primitives use a `value` attribute; reflected objects use Deserialize().
     */
    template <typename Archive, typename Elem, typename Alloc>
    void DeserializeField(Archive& d, eastl::string_view name, eastl::vector<Elem, Alloc>& vec) {
        size_t count = 0;
        if (d.BeginArray(name, "item", count)) {
            vec.clear();
            vec.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (d.BeginArrayElement()) {
                    Elem item{};
                    Detail::DeserializeElement(d, item);
                    vec.push_back(eastl::move(item));
                    d.EndArrayElement();
                }
            }
            d.EndArray();
        }
    }

    // =========================================================================
    // eastl::unordered_map
    // =========================================================================

    /**
     * @brief Serialize an unordered_map.
     *
     * Each key-value pair becomes an `<entry key="...">` element.
     * The value is serialized into the element body (attributes/children).
     * XML: <fieldName><entry key="k1" .../><entry key="k2" .../></fieldName>
     */
    template <typename Archive, typename K, typename V, typename Hash, typename Pred, typename Alloc>
    void SerializeField(Archive& s, eastl::string_view name, const eastl::unordered_map<K, V, Hash, Pred, Alloc>& map) {
        size_t count = map.size();
        if (s.BeginArray(name, "entry", count)) {
            for (const auto& [key, value] : map) {
                if (s.BeginArrayElement()) {
                    s.WriteAttribute("key", key);
                    Detail::SerializeElement(s, value);
                    s.EndArrayElement();
                }
            }
            s.EndArray();
        }
    }

    /**
     * @brief Deserialize an unordered_map.
     *
     * Reads each `<entry key="...">` element, deserializing the key from the `key`
     * attribute and the value from the element body.
     */
    template <typename Archive, typename K, typename V, typename Hash, typename Pred, typename Alloc>
    void DeserializeField(Archive& d, eastl::string_view name, eastl::unordered_map<K, V, Hash, Pred, Alloc>& map) {
        size_t count = 0;
        if (d.BeginArray(name, "entry", count)) {
            map.clear();
            map.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (d.BeginArrayElement()) {
                    K key{};
                    d.ReadAttribute("key", key);
                    V value{};
                    Detail::DeserializeElement(d, value);
                    map.emplace(eastl::move(key), eastl::move(value));
                    d.EndArrayElement();
                }
            }
            d.EndArray();
        }
    }

    // =========================================================================
    // Primitive catch-all (selected last — lowest priority)
    // =========================================================================

    /**
     * @brief Serialize any primitive value as a named XML attribute.
     *
     * This is the lowest-priority overload. All more-specific overloads
     * (reflected objects, vectors, maps, smart pointers) are preferred by
     * the compiler. This catch-all handles plain scalars and strings that
     * are not matched by any other overload.
     */
    template <typename Archive, typename T>
    void SerializeField(Archive& s, eastl::string_view name, const T& value) {
        s.WriteAttribute(name, value);
    }

    /**
     * @brief Deserialize any primitive value from a named XML attribute.
     *
     * Lowest-priority overload, see SerializeField catch-all above.
     */
    template <typename Archive, typename T>
    void DeserializeField(Archive& d, eastl::string_view name, T& value) {
        d.ReadAttribute(name, value);
    }

}  // namespace BECore
