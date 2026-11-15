#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/SerializationTraits.h>
#include <BECore/Utils/EnumUtils.h>
#include <array>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

namespace BECore {

    /**
     * @brief Dispatch helpers for IPropertyVisitor, analogous to Ser::Save/Ser::Load
     *
     * PropVisit::Visit(visitor, field, key) selects the right overload based on the
     * field type:
     *   - Arithmetic types              → visitor.Visit(key, field) directly
     *   - PoolString / String (and any string type) → visitor.Visit(key, field) directly
     *   - CORE_ENUM (IsReflectedEnum)   → visitor.VisitEnum(key, names, count, index)
     *   - Types with VisitProperties()  → BeginCompound + VisitProperties + EndCompound
     *   - BE_CLASS T by value           → BeginCompound + AcceptPropertyVisitor + EndCompound
     *   - IntrusivePtr<BE_CLASS T>      → BeginCompound + ptr->AcceptPropertyVisitor + EndCompound
     *   - vector<IntrusivePtr<T>>       → BeginCompound + iterate elements + EndCompound (no add, no remove: the class of
     *                                     a new element would be a choice no hook offers)
     *   - Vector, SmallVector, FixedVector, Array (and std::array)
     *                                   → BeginList + per element (Visit under `[i]`, EndListElement) + EndList; an Array
     *                                     is never resized, a FixedVector that cannot overflow adds only while it has room
     *   - HashMap, FixedHashMap, Map    → the same, the values under the text of their key; VisitNewKey before EndList
     *                                     when a text or integer key lets an add happen
     *   - Pair (eastl:: and std::)      → BeginCompound + `first` + `second` + EndCompound
     *   - Optional (eastl:: and std::)  → VisitPresence, then the value under the field's own name when it holds one
     *   - Variant                       → BeginCompound + VisitAlternative + the active alternative under `value` + EndCompound
     *
     * The elements of a container are any type of this list, containers included. An add or a remove a visitor requests
     * is applied by the dispatch after EndList, never during the loop.
     *
     * PropVisit::VisitField(visitor, attributes, visit) wraps the visit of one field of a reflected class in
     * BeginField / EndField and leaves out a `hidden` field; the generated AcceptPropertyVisitor calls it with a
     * lambda that calls PropVisit::Visit.
     *
     * Generated AcceptPropertyVisitor implementations call these helpers.
     */
    namespace PropVisit {

        // =====================================================================
        // Detail helpers for intrusive pointer dispatch
        // =====================================================================

        namespace Detail {

            template <typename Ptr>
            struct IntrusivePtrPointee {};

            template <typename T>
            struct IntrusivePtrPointee<IntrusivePtrAtomic<T>> {
                using type = T;
            };

            template <typename T>
            struct IntrusivePtrPointee<IntrusivePtrNonAtomic<T>> {
                using type = T;
            };

            template <typename Ptr>
            concept IsIntrusivePtr = requires {
                typename IntrusivePtrPointee<Ptr>::type;
            };

        }  // namespace Detail

        // =====================================================================
        // Arithmetic types
        // =====================================================================

        template <typename T>
        requires std::is_arithmetic_v<T> bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            return v.Visit(key, value);
        }

        // =====================================================================
        // PoolString
        // =====================================================================

        inline bool Visit(IPropertyVisitor& v, PoolString& value, eastl::string_view key) {
            return v.Visit(key, value);
        }

        // =====================================================================
        // Strings: String, a string of any allocator, FixedString
        // =====================================================================

        namespace Detail {

            template <typename Text>
            bool VisitText(IPropertyVisitor& v, Text& value, eastl::string_view key) {
                if constexpr (std::is_same_v<Text, String>) {
                    return v.Visit(key, value);
                } else {
                    // The visitor edits a String: edit a copy and take it back when it changed
                    String text(value.data(), value.size());
                    if (!v.Visit(key, text)) {
                        return false;
                    }
                    ::BECore::Detail::AssignText(value, StringView(text.data(), text.size()));
                    return true;
                }
            }

        }  // namespace Detail

        template <typename Alloc>
        bool Visit(IPropertyVisitor& v, eastl::basic_string<char, Alloc>& value, eastl::string_view key) {
            return Detail::VisitText(v, value, key);
        }

        // Its own overload: through the base above, the type would be a basic_string and the length limit of the FixedString lost
        template <int N, bool Overflow, typename OverflowAlloc>
        bool Visit(IPropertyVisitor& v, eastl::fixed_string<char, N, Overflow, OverflowAlloc>& value, eastl::string_view key) {
            return Detail::VisitText(v, value, key);
        }

        // =====================================================================
        // Compound types (with VisitProperties method)
        // =====================================================================

        template <typename T>
        requires requires(T& t, IPropertyVisitor& visitor) {
            {
                t.VisitProperties(visitor)
            } -> std::same_as<bool>;
        }
        bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            if (v.BeginCompound(key)) {
                bool changed = value.VisitProperties(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // IntrusivePtr<T> (with AcceptPropertyVisitor)
        // =====================================================================

        template <typename Ptr>
        requires(Detail::IsIntrusivePtr<Ptr>&& requires(typename Detail::IntrusivePtrPointee<Ptr>::type& t, IPropertyVisitor& v) {
            {
                t.AcceptPropertyVisitor(v)
            } -> std::same_as<bool>;
        }) bool Visit(IPropertyVisitor& v, Ptr& ptr, eastl::string_view key) {
            if (!ptr) {
                return false;
            }
            if (v.BeginCompound(key)) {
                bool changed = ptr->AcceptPropertyVisitor(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // eastl::vector<IntrusivePtr<T>>
        // =====================================================================

        template <typename Ptr, typename Alloc>
        requires Detail::IsIntrusivePtr<Ptr> bool Visit(IPropertyVisitor& v, eastl::vector<Ptr, Alloc>& vec, eastl::string_view key) {
            bool changed = false;
            if (v.BeginCompound(key)) {
                char indexBuf[32];
                for (size_t i = 0; i < vec.size(); ++i) {
                    snprintf(indexBuf, sizeof(indexBuf), "[%zu]", i);
                    changed |= Visit(v, vec[i], eastl::string_view(indexBuf));
                }
                v.EndCompound();
            }
            return changed;
        }

        // =====================================================================
        // BE_CLASS types embedded by value (have AcceptPropertyVisitor directly)
        // =====================================================================

        template <typename T>
        requires(!Detail::IsIntrusivePtr<T> && !IsReflectedEnum<T> && !std::is_arithmetic_v<T> && requires(T & t, IPropertyVisitor& v) {
            {
                t.AcceptPropertyVisitor(v)
            } -> std::same_as<bool>;
        }) bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            if (v.BeginCompound(key)) {
                bool changed = value.AcceptPropertyVisitor(v);
                v.EndCompound();
                return changed;
            }
            return false;
        }

        // =====================================================================
        // One field of a reflected class
        // =====================================================================

        /**
         * @brief Visit one reflected field between BeginField and EndField
         * @param v The visitor
         * @param attributes The attributes of the field (`ReflectionTraits<T>::fields`)
         * @param visit Callable that visits the field and returns whether it changed, `[&] { return PropVisit::Visit(v, field, key); }`
         * @return true when the visit changed the value; false for a field left out
         *
         * A `hidden` field is not visited unless the visitor's `ShowsHidden()` is true: it gets no `BeginField` either.
         * The visit is a callable, not the field, because the overloads of `Visit` are found where the generated code is
         * written: a type that has its own `PropVisit::Visit` overload declared next to its class is found there and not here.
         */
        template <typename Fn>
        bool VisitField(IPropertyVisitor& v, const FieldAttributes& attributes, Fn&& visit) {
            if (attributes.hidden && !v.ShowsHidden()) {
                return false;
            }
            v.BeginField(attributes);
            const bool changed = visit();
            v.EndField();
            return changed;
        }

        // =====================================================================
        // CORE_ENUM / IsReflectedEnum types
        // =====================================================================

        template <typename T>
        requires IsReflectedEnum<T> bool Visit(IPropertyVisitor& v, T& value, eastl::string_view key) {
            const auto& names = EnumUtils<T>::Names();
            const auto& values = EnumUtils<T>::Values();
            size_t selected = 0;
            for (size_t i = 0; i < values.size(); ++i) {
                if (values[i] == value) {
                    selected = i;
                    break;
                }
            }
            if (v.VisitEnum(key, names.data(), names.size(), selected)) {
                value = values[selected];
                return true;
            }
            return false;
        }

        // =====================================================================
        // Containers: sequences, maps, Pair, Optional, Variant
        // =====================================================================
        //
        // All declared before any is defined: an element is visited by an unqualified call in a template, which finds the
        // overloads declared before that template and, at instantiation, those of the arguments' own namespaces (ADL), never a
        // later overload of PropVisit. So a container inside a container is found here, while an element whose type has its own
        // PropVisit::Visit declared after this header (UIColor) is visited by the overload this header has for it (its
        // VisitProperties), not by its own: the same rule as VisitField's.

        template <typename T, typename Alloc>
        requires(!Detail::IsIntrusivePtr<T>) bool Visit(IPropertyVisitor& v, eastl::vector<T, Alloc>& items, StringView key);

        // Its own overload: through the base above, the room limit of the fixed vector would be lost
        template <typename T, size_t N, bool Overflow, typename OverflowAlloc>
        requires(!Detail::IsIntrusivePtr<T>) bool Visit(IPropertyVisitor& v, eastl::fixed_vector<T, N, Overflow, OverflowAlloc>& items, StringView key);

        template <typename T, size_t N>
        bool Visit(IPropertyVisitor& v, eastl::array<T, N>& items, StringView key);

        template <typename T, size_t N>
        bool Visit(IPropertyVisitor& v, std::array<T, N>& items, StringView key);

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc, bool CacheHashCode>
        bool Visit(IPropertyVisitor& v, eastl::hash_map<K, V, Hash, Pred, Alloc, CacheHashCode>& map, StringView key);

        // Its own overload, like the fixed vector's
        template <typename K, typename V, size_t NodeCount, size_t BucketCount, bool Overflow, typename Hash, typename Pred, bool CacheHashCode, typename OverflowAlloc>
        bool Visit(IPropertyVisitor& v, eastl::fixed_hash_map<K, V, NodeCount, BucketCount, Overflow, Hash, Pred, CacheHashCode, OverflowAlloc>& map, StringView key);

        template <typename K, typename V, typename Cmp, typename Alloc>
        bool Visit(IPropertyVisitor& v, eastl::map<K, V, Cmp, Alloc>& map, StringView key);

        template <typename A, typename B>
        bool Visit(IPropertyVisitor& v, eastl::pair<A, B>& value, StringView key);

        template <typename A, typename B>
        bool Visit(IPropertyVisitor& v, std::pair<A, B>& value, StringView key);

        template <typename T>
        bool Visit(IPropertyVisitor& v, eastl::optional<T>& value, StringView key);

        template <typename T>
        bool Visit(IPropertyVisitor& v, std::optional<T>& value, StringView key);

        template <typename... Ts>
        bool Visit(IPropertyVisitor& v, std::variant<Ts...>& value, StringView key);

        /// The empty alternative of a Variant: nothing to edit
        inline bool Visit(IPropertyVisitor& /*v*/, std::monostate& /*value*/, StringView /*key*/) {
            return false;
        }

        namespace Detail {

            /// Room for the label of one element (`[i]`, an integer key): the label lives on the stack, the inspector runs per frame
            inline constexpr size_t kLabelCapacity = 32;

            using LabelBuffer = char[kLabelCapacity];

            template <typename... Args>
            StringView FormatLabel(LabelBuffer& buffer, FormatString<Args...> format, Args&&... args) {
                const FormatResult written = FormatToN(buffer, kLabelCapacity, format, std::forward<Args>(args)...);
                return StringView(buffer, written.truncated ? kLabelCapacity : written.size);
            }

            template <typename K>
            concept IsIntegerKey = std::is_integral_v<K> && !std::is_same_v<K, bool>;

            template <typename K>
            concept IsTextKey = std::is_same_v<K, PoolString> || requires(K & k, StringView text) {
                k.assign(text.data(), text.size());
                StringView(k.data(), k.size());
                requires std::is_same_v<typename K::value_type, char>;
            };

            template <typename K>
            StringView KeyLabel(LabelBuffer& buffer, const K& key, size_t position) {
                if constexpr (std::is_same_v<K, PoolString>) {
                    return key.ToStringView();
                } else if constexpr (IsTextKey<K>) {
                    return StringView(key.data(), key.size());
                } else if constexpr (IsReflectedEnum<K>) {
                    return EnumUtils<K>::ToString(key);
                } else if constexpr (IsIntegerKey<K>) {
                    return FormatLabel(buffer, "{}", key);
                } else {
                    return FormatLabel(buffer, "[{}]", position);
                }
            }

            /// Removes the element at @p position of the container's own iteration
            template <typename Container>
            void EraseAt(Container& container, size_t position) {
                auto it = container.begin();
                eastl::advance(it, static_cast<ptrdiff_t>(position));
                container.erase(it);
            }

            /// @p Resizable: elements can be added and removed (an Array cannot); a fixed container that cannot overflow adds only while it has room
            template <bool Resizable, typename Container>
            bool VisitSequence(IPropertyVisitor& v, Container& items, StringView key) {
                using Element = typename Container::value_type;
                ListInfo info{items.size(), false, false};
                if constexpr (Resizable) {
                    info.canAdd = std::is_default_constructible_v<Element> && !Ser::Detail::IsFull(items);
                    info.canRemove = !items.empty();
                }
                if (!v.BeginList(key, info)) {
                    return false;
                }
                bool changed = false;
                ListRequest request;
                LabelBuffer label;
                for (size_t i = 0; i < items.size(); ++i) {
                    changed |= Visit(v, items[i], FormatLabel(label, "[{}]", i));
                    v.EndListElement(i, request);
                }
                v.EndList(request);
                if constexpr (Resizable) {
                    if (request.action == ListAction::Add && info.canAdd) {
                        if constexpr (std::is_default_constructible_v<Element>) {
                            items.emplace_back();
                            changed = true;
                        }
                    } else if (request.action == ListAction::Remove && info.canRemove && request.index < items.size()) {
                        EraseAt(items, request.index);
                        changed = true;
                    }
                }
                return changed;
            }

            /// The key an add would use: what the visitor may change in VisitNewKey before it is turned into the map's key type
            template <typename K>
            using NewKey = std::conditional_t<IsIntegerKey<K>, int64_t, String>;

            /// Whether @p value is a value of the integer type @p K (std::in_range refuses the character types, which can be keys)
            template <typename K>
            bool FitsKey(int64_t value) {
                if constexpr (std::is_signed_v<K>) {
                    return value >= std::numeric_limits<K>::min() && value <= std::numeric_limits<K>::max();
                } else {
                    return value >= 0 && static_cast<uint64_t>(value) <= std::numeric_limits<K>::max();
                }
            }

            /// The smallest non-negative integer that is no key of @p map yet; false when every value of the key type is one
            template <typename Map>
            bool FirstFreeIntegerKey(const Map& map, int64_t& key) {
                using K = typename Map::key_type;
                for (int64_t candidate = 0; candidate <= static_cast<int64_t>(map.size()); ++candidate) {
                    if (!FitsKey<K>(candidate)) {
                        return false;
                    }
                    if (map.find(static_cast<K>(candidate)) == map.end()) {
                        key = candidate;
                        return true;
                    }
                }
                return false;
            }

            /// Adds a default value under @p newKey; false when the key does not fit the key type or the map has it already
            template <typename Map>
            bool AddEntry(Map& map, const NewKey<typename Map::key_type>& newKey) {
                using K = typename Map::key_type;
                K mapKey{};
                if constexpr (IsIntegerKey<K>) {
                    if (!FitsKey<K>(newKey)) {
                        return false;
                    }
                    mapKey = static_cast<K>(newKey);
                } else if constexpr (std::is_same_v<K, PoolString>) {
                    mapKey = PoolString::Intern(StringView(newKey.data(), newKey.size()));
                } else {
                    ::BECore::Detail::AssignText(mapKey, StringView(newKey.data(), newKey.size()));
                }
                if (map.find(mapKey) != map.end()) {
                    return false;
                }
                map.emplace(eastl::move(mapKey), typename Map::mapped_type{});
                return true;
            }

            /// The values of a map, under the text of their keys; an add takes its key from VisitNewKey
            template <typename Map>
            bool VisitMap(IPropertyVisitor& v, Map& map, StringView key) {
                using K = typename Map::key_type;
                constexpr bool kKeyFromVisitor = (IsIntegerKey<K> || IsTextKey<K>) && std::is_default_constructible_v<typename Map::mapped_type>;
                NewKey<K> newKey{};
                ListInfo info{map.size(), false, !map.empty()};
                if constexpr (kKeyFromVisitor) {
                    info.canAdd = !Ser::Detail::IsFull(map);
                    if constexpr (IsIntegerKey<K>) {
                        info.canAdd = info.canAdd && FirstFreeIntegerKey(map, newKey);
                    }
                }
                if (!v.BeginList(key, info)) {
                    return false;
                }
                bool changed = false;
                ListRequest request;
                LabelBuffer label;
                size_t position = 0;
                for (auto& entry : map) {
                    changed |= Visit(v, entry.second, KeyLabel(label, entry.first, position));
                    v.EndListElement(position, request);
                    ++position;
                }
                if constexpr (kKeyFromVisitor) {
                    if (info.canAdd) {
                        v.VisitNewKey(newKey);
                    }
                }
                v.EndList(request);
                if (request.action == ListAction::Add && info.canAdd) {
                    if constexpr (kKeyFromVisitor) {
                        changed |= AddEntry(map, newKey);
                    }
                } else if (request.action == ListAction::Remove && info.canRemove && request.index < map.size()) {
                    EraseAt(map, request.index);
                    changed = true;
                }
                return changed;
            }

            template <typename A, typename B>
            bool VisitPair(IPropertyVisitor& v, A& first, B& second, StringView key) {
                if (!v.BeginCompound(key)) {
                    return false;
                }
                bool changed = Visit(v, first, "first");
                changed |= Visit(v, second, "second");
                v.EndCompound();
                return changed;
            }

            template <typename Opt>
            bool VisitOptional(IPropertyVisitor& v, Opt& value, StringView key) {
                bool changed = false;
                bool present = value.has_value();
                if (v.VisitPresence(key, present) && present != value.has_value()) {
                    if (!present) {
                        value.reset();
                        changed = true;
                    } else if constexpr (std::is_default_constructible_v<typename Opt::value_type>) {
                        value.emplace();
                        changed = true;
                    }
                }
                if (value.has_value()) {
                    changed |= Visit(v, *value, key);
                }
                return changed;
            }

            template <typename Var, size_t... I>
            void EmplaceAlternative(Var& value, size_t index, std::index_sequence<I...> /*indices*/) {
                static_cast<void>(((index == I ? (value.template emplace<I>(), true) : false) || ...));
            }

            template <typename Var, size_t... I>
            bool VisitActiveAlternative(IPropertyVisitor& v, Var& value, std::index_sequence<I...> /*indices*/) {
                const size_t active = value.index();
                bool changed = false;
                static_cast<void>(((active == I ? (changed = Visit(v, std::get<I>(value), Ser::Detail::kVariantValueKey), true) : false) || ...));
                return changed;
            }

        }  // namespace Detail

        template <typename T, typename Alloc>
        requires(!Detail::IsIntrusivePtr<T>) bool Visit(IPropertyVisitor& v, eastl::vector<T, Alloc>& items, StringView key) {
            return Detail::VisitSequence<true>(v, items, key);
        }

        template <typename T, size_t N, bool Overflow, typename OverflowAlloc>
        requires(!Detail::IsIntrusivePtr<T>) bool Visit(IPropertyVisitor& v, eastl::fixed_vector<T, N, Overflow, OverflowAlloc>& items, StringView key) {
            return Detail::VisitSequence<true>(v, items, key);
        }

        template <typename T, size_t N>
        bool Visit(IPropertyVisitor& v, eastl::array<T, N>& items, StringView key) {
            return Detail::VisitSequence<false>(v, items, key);
        }

        template <typename T, size_t N>
        bool Visit(IPropertyVisitor& v, std::array<T, N>& items, StringView key) {
            return Detail::VisitSequence<false>(v, items, key);
        }

        template <typename K, typename V, typename Hash, typename Pred, typename Alloc, bool CacheHashCode>
        bool Visit(IPropertyVisitor& v, eastl::hash_map<K, V, Hash, Pred, Alloc, CacheHashCode>& map, StringView key) {
            return Detail::VisitMap(v, map, key);
        }

        template <typename K, typename V, size_t NodeCount, size_t BucketCount, bool Overflow, typename Hash, typename Pred, bool CacheHashCode, typename OverflowAlloc>
        bool Visit(IPropertyVisitor& v, eastl::fixed_hash_map<K, V, NodeCount, BucketCount, Overflow, Hash, Pred, CacheHashCode, OverflowAlloc>& map, StringView key) {
            return Detail::VisitMap(v, map, key);
        }

        template <typename K, typename V, typename Cmp, typename Alloc>
        bool Visit(IPropertyVisitor& v, eastl::map<K, V, Cmp, Alloc>& map, StringView key) {
            return Detail::VisitMap(v, map, key);
        }

        template <typename A, typename B>
        bool Visit(IPropertyVisitor& v, eastl::pair<A, B>& value, StringView key) {
            return Detail::VisitPair(v, value.first, value.second, key);
        }

        template <typename A, typename B>
        bool Visit(IPropertyVisitor& v, std::pair<A, B>& value, StringView key) {
            return Detail::VisitPair(v, value.first, value.second, key);
        }

        template <typename T>
        bool Visit(IPropertyVisitor& v, eastl::optional<T>& value, StringView key) {
            return Detail::VisitOptional(v, value, key);
        }

        template <typename T>
        bool Visit(IPropertyVisitor& v, std::optional<T>& value, StringView key) {
            return Detail::VisitOptional(v, value, key);
        }

        template <typename... Ts>
        bool Visit(IPropertyVisitor& v, std::variant<Ts...>& value, StringView key) {
            static_assert((std::is_default_constructible_v<Ts> && ...), "Every alternative of a visited Variant must be default constructible: a switch emplaces a default value");
            if (!v.BeginCompound(key)) {
                return false;
            }
            bool changed = false;
            size_t index = value.index();
            if (v.VisitAlternative(key, sizeof...(Ts), index) && index != value.index() && index < sizeof...(Ts)) {
                Detail::EmplaceAlternative(value, index, std::index_sequence_for<Ts...>{});
                changed = true;
            }
            changed |= Detail::VisitActiveAlternative(v, value, std::index_sequence_for<Ts...>{});
            v.EndCompound();
            return changed;
        }

    }  // namespace PropVisit

}  // namespace BECore
