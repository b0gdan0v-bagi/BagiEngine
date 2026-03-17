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
         * @brief Trait to extract the pointee type from any IntrusivePtr.
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
         * @brief Concept: IntrusivePtr whose pointee has a FactoryTraits specialization.
         *
         * Replaces the old InnerFactoryType struct — derived directly from IsIntrusivePtr
         * and HasFactory so there are no constrained partial specializations.
         */
        template <typename Ptr>
        concept HasInnerFactory = IsIntrusivePtr<Ptr> && HasFactory<typename IntrusivePtrPointeeType<Ptr>::type>;

        /**
         * @brief Concept that matches types with an Initialize() method.
         */
        template <typename T>
        concept HasInitialize = requires(T & t) {
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
         * @brief Write the body of an IntrusivePtr into the current (already open) archive node.
         *
         * For factory types writes the "type" attribute, then delegates to Serialize().
         * Caller is responsible for BeginObject/EndObject around this call.
         */
        template <typename Archive, typename Ptr>
        requires IsIntrusivePtr<Ptr> void SerializeIntrusiveBody(Archive& s, const Ptr& ptr) {
            using T = typename IntrusivePtrPointeeType<Ptr>::type;
            if constexpr (HasFactory<T>)
                s.WriteAttribute("type", ptr->GetTypeMeta().typeName);
            if constexpr (HasSerialize<T, Archive>)
                ptr->Serialize(s);
        }

        /**
         * @brief Read the body of an IntrusivePtr from the current (already open) archive node.
         *
         * If ptr is null: tries factory-first (reads "type", creates via AbstractFactory<T>),
         * then falls back to New<T> for non-abstract types. If ptr is already set, reuses it.
         * Deserialize + Initialize are called exactly once after creation.
         * Caller is responsible for BeginObject/EndObject around this call.
         */
        template <typename Archive, typename Ptr>
        requires IsIntrusivePtr<Ptr> void DeserializeIntrusiveBody(Archive& d, Ptr& ptr) {
            using T = typename IntrusivePtrPointeeType<Ptr>::type;
            if (!ptr) {
                eastl::string typeStr;
                if (d.ReadAttribute("type", typeStr)) {
                    ptr = AbstractFactory<T>::GetInstance().Create(eastl::string_view(typeStr.data(), typeStr.size()));
                }

                if (!ptr) {
                    if constexpr (!std::is_abstract_v<T>)
                        ptr = New<T>();
                }
            }
            if (ptr) {
                ptr->Deserialize(d);
                if constexpr (HasInitialize<T>)
                    ptr->Initialize();
            }
        }

        /**
         * @brief Serialize a single element into the current (already open) array element node.
         *
         * Used by vector/map loops. Delegates IntrusivePtr logic to SerializeIntrusiveBody.
         */
        template <typename Archive, typename T>
        void SerializeElement(Archive& s, const T& item) {
            if constexpr (IsIntrusivePtr<T>) {
                if (item)
                    SerializeIntrusiveBody(s, item);
            } else if constexpr (HasSerialize<T, Archive>) {
                item.Serialize(s);
            } else {
                s.WriteAttribute("value", item);
            }
        }

        /**
         * @brief Deserialize a single element from the current (already open) array element node.
         *
         * Used by vector/map loops. Delegates IntrusivePtr logic to DeserializeIntrusiveBody.
         */
        template <typename Archive, typename T>
        void DeserializeElement(Archive& d, T& item) {
            if constexpr (IsIntrusivePtr<T>) {
                DeserializeIntrusiveBody(d, item);
            } else if constexpr (HasDeserialize<T, Archive>) {
                item.Deserialize(d);
            } else {
                d.ReadAttribute("value", item);
            }
        }

        /**
         * @brief Construct a default T and deserialize it from the current archive node.
         *
         * Convenience wrapper over DeserializeElement for use in push_back / emplace sites.
         */
        template <typename T, typename Archive>
        T MakeElement(Archive& d) {
            T item{};
            DeserializeElement(d, item);
            return item;
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
     * @brief Serialize an IntrusivePtr as a named child element.
     *
     * No-op if the pointer is null. For factory types writes the "type" attribute;
     * otherwise delegates to the pointee's Serialize().
     */
    template <typename Archive, typename Ptr>
    requires Detail::IsIntrusivePtr<Ptr> void SerializeField(Archive& s, eastl::string_view name, const Ptr& ptr) {
        if (ptr && s.BeginObject(name)) {
            Detail::SerializeIntrusiveBody(s, ptr);
            s.EndObject();
        }
    }

    /**
     * @brief Deserialize into an IntrusivePtr from a named child element.
     *
     * For factory types reads "type" and creates via AbstractFactory (ptr may be null on entry).
     * For concrete types deserializes into existing ptr, or creates via New<T>.
     */
    template <typename Archive, typename Ptr>
    requires Detail::IsIntrusivePtr<Ptr> void DeserializeField(Archive& d, eastl::string_view name, Ptr& ptr) {
        if (d.BeginObject(name)) {
            Detail::DeserializeIntrusiveBody(d, ptr);
            d.EndObject();
        }
    }

    // =========================================================================
    // eastl::vector — polymorphic factory case (more constrained, selected first)
    // =========================================================================

    /**
     * @brief Serialize a vector of factory-managed polymorphic pointers.
     *
     * XML: <fieldName><elementName type="ConcreteClassName" enabled="true" .../></fieldName>
     */
    template <typename Archive, typename Ptr, typename Alloc>
    requires Detail::HasInnerFactory<Ptr> void SerializeField(Archive& s, eastl::string_view name, const eastl::vector<Ptr, Alloc>& vec) {
        using Base = typename Detail::IntrusivePtrPointeeType<Ptr>::type;
        using Traits = FactoryTraits<Base>;

        size_t count = vec.size();
        if (s.BeginArray(name, Traits::elementName, count)) {
            for (const auto& item : vec) {
                if (item && s.BeginArrayElement()) {
                    s.WriteAttribute("enabled", true);
                    Detail::SerializeIntrusiveBody(s, item);
                    s.EndArrayElement();
                }
            }
            s.EndArray();
        }
    }

    /**
     * @brief Deserialize a vector of factory-managed polymorphic pointers.
     *
     * Reads "type" and optional "enabled" per element.
     * Creates instances via AbstractFactory<Base> — no derived-class headers needed.
     * Disabled elements (enabled="false") are skipped.
     */
    template <typename Archive, typename Ptr, typename Alloc>
    requires Detail::HasInnerFactory<Ptr> void DeserializeField(Archive& d, eastl::string_view name, eastl::vector<Ptr, Alloc>& vec) {
        using Base = typename Detail::IntrusivePtrPointeeType<Ptr>::type;
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
                        Ptr item;
                        Detail::DeserializeIntrusiveBody(d, item);
                        if (item)
                            vec.push_back(eastl::move(item));
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
     * Each element is serialized into an <item> child element.
     * Primitives use a "value" attribute; reflected objects use Serialize().
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
     * Each <item> element is deserialized in order.
     * Primitives use a "value" attribute; reflected objects use Deserialize().
     */
    template <typename Archive, typename Elem, typename Alloc>
    void DeserializeField(Archive& d, eastl::string_view name, eastl::vector<Elem, Alloc>& vec) {
        size_t count = 0;
        if (d.BeginArray(name, "item", count)) {
            vec.clear();
            vec.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                if (d.BeginArrayElement()) {
                    const auto elem = Detail::MakeElement<Elem>(d);
                    vec.push_back(elem);
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
     * Each key-value pair becomes an <entry key="..."> element.
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
     * Reads each <entry key="..."> element, deserializing key from the "key" attribute
     * and value from the element body.
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
                    map.emplace(eastl::move(key), Detail::MakeElement<V>(d));
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
     * Lowest-priority overload — all more-specific overloads (reflected objects,
     * vectors, maps, smart pointers) are preferred by the compiler.
     * Handles plain scalars, strings, and PoolString.
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
