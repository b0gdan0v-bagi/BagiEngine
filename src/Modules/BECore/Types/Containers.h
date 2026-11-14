#pragma once

#include <BECore/Types/Strings.h>
#include <EASTL/array.h>
#include <EASTL/fixed_hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/map.h>
#include <EASTL/memory.h>
#include <EASTL/set.h>
#include <EASTL/vector_map.h>
#include <EASTL/vector_set.h>

// The container types of the engine: names for what the code uses, so the implementation is chosen here, once. All but the
// maps keyed by String are plain `using`: the same type as the eastl:: name.
namespace BECore {

    /// The hash, equality and order a key gets by default: those of the key, except for String, whose map looks up by StringView
    /// and const char* without making a String (StringHash, StringEqual, StringLess).
    template <typename Key>
    using DefaultHash = std::conditional_t<std::is_same_v<Key, String>, StringHash, eastl::hash<Key>>;
    template <typename Key>
    using DefaultEqual = std::conditional_t<std::is_same_v<Key, String>, StringEqual, eastl::equal_to<Key>>;
    template <typename Key>
    using DefaultLess = std::conditional_t<std::is_same_v<Key, String>, StringLess, eastl::less<Key>>;

    /// The growing array.
    template <typename T>
    using Vector = eastl::vector<T>;

    /// A vector with N elements inside the object that moves to the heap when it grows past N.
    template <typename T, size_t N>
    using SmallVector = eastl::fixed_vector<T, N, true>;

    /// A vector with room for exactly N elements, all inside the object; it never allocates and never grows past N.
    template <typename T, size_t N>
    using FixedVector = eastl::fixed_vector<T, N, false>;

    /// The array of a size known at compile time.
    template <typename T, size_t N>
    using Array = eastl::array<T, N>;

    /// A view of a contiguous range of T (a fixed Extent is the one parameter eastl::span has).
    template <typename T, size_t Extent = eastl::dynamic_extent>
    using Span = eastl::span<T, Extent>;

    /// The hash table: the default container for a key lookup. A String key hashes and compares by StringView, so `find`,
    /// `contains`, `count` and `at` take a StringView or a literal without making a String.
    template <typename Key, typename T, typename Hash = DefaultHash<Key>, typename Equal = DefaultEqual<Key>>
    using HashMap = eastl::hash_map<Key, T, Hash, Equal>;

    /// The ordered map (a red-black tree); a String key compares by StringView as HashMap's does.
    template <typename Key, typename T, typename Compare = DefaultLess<Key>>
    using Map = eastl::map<Key, T, Compare>;

    /// The hash set: a HashMap without values. A String key hashes and compares as HashMap's does, so `find`, `contains`, `count`
    /// and `erase` take a StringView or a literal without making a String.
    template <typename Key, typename Hash = DefaultHash<Key>, typename Equal = DefaultEqual<Key>>
    using HashSet = eastl::hash_set<Key, Hash, Equal>;

    /// The ordered set (a red-black tree); a String key compares by StringView as Map's does. EASTL has no `extract` or `merge`.
    template <typename Key, typename Compare = DefaultLess<Key>>
    using Set = eastl::set<Key, Compare>;

    /// A hash map with its nodes inside the object (BucketCount: how many buckets; Overflow: whether it may take heap nodes when full).
    template <typename Key, typename T, size_t NodeCount, size_t BucketCount = NodeCount + 1, bool Overflow = true, typename Hash = DefaultHash<Key>, typename Equal = DefaultEqual<Key>>
    using FixedHashMap = eastl::fixed_hash_map<Key, T, NodeCount, BucketCount, Overflow, Hash, Equal>;

    /// A map kept as a sorted vector (eastl::vector_map). For new code, instead of HashMap, when the map is small, rarely changed
    /// and iterated often: lookup is a binary search, iteration is a walk of one block of memory, insertion moves elements.
    template <typename Key, typename T, typename Compare = DefaultLess<Key>>
    using FlatMap = eastl::vector_map<Key, T, Compare>;

    /// The set kept as a sorted vector (eastl::vector_set); the same rule as FlatMap.
    template <typename Key, typename Compare = DefaultLess<Key>>
    using FlatSet = eastl::vector_set<Key, Compare>;

    /// A value or nothing. std::optional, not eastl::optional: the standard one, which the other libraries speak.
    template <typename T>
    using Optional = std::optional<T>;

    /// The owner of one object (or an array, `UniquePtr<T[]>`); move-only.
    template <typename T, typename Deleter = eastl::default_delete<T>>
    using UniquePtr = eastl::unique_ptr<T, Deleter>;

    /// Makes a `UniquePtr<T>` from the constructor arguments, or a `UniquePtr<T[]>` of n default-initialized elements (`MakeUnique<T[]>(n)`).
    template <typename T, typename... Args>
    [[nodiscard]] auto MakeUnique(Args&&... args) {
        return eastl::make_unique<T>(eastl::forward<Args>(args)...);
    }

    /// The shared owner of one object, counted. The control block of MakeShared is allocated with the object.
    template <typename T>
    using SharedPtr = eastl::shared_ptr<T>;

    template <typename T, typename... Args>
    [[nodiscard]] SharedPtr<T> MakeShared(Args&&... args) {
        return eastl::make_shared<T>(eastl::forward<Args>(args)...);
    }

    /// Two values; the value_type of every EASTL map. Structured bindings work (`auto [a, b] = pair`), `std::tie(a, b) = pair` does not.
    template <typename A, typename B>
    using Pair = eastl::pair<A, B>;

    template <typename A, typename B>
    [[nodiscard]] constexpr auto MakePair(A&& first, B&& second) {
        return eastl::make_pair(eastl::forward<A>(first), eastl::forward<B>(second));
    }

    /// One of several types. std::variant, like Optional: the standard one; `std::visit`, `std::get`, `std::get_if`,
    /// `std::holds_alternative` and `std::monostate` stay spelled `std::`.
    template <typename... Ts>
    using Variant = std::variant<Ts...>;

    /// Several values of different types. std::tuple: the reflection traits store fields in one; `std::apply` and `std::get` stay spelled `std::`.
    template <typename... Ts>
    using Tuple = std::tuple<Ts...>;

}  // namespace BECore
