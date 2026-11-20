#pragma once

#include <BECore/Types/Strings.h>
#include <EASTL/array.h>
#include <EASTL/fixed_hash_map.h>
#include <EASTL/fixed_vector.h>
#include <EASTL/functional.h>
#include <EASTL/hash_map.h>
#include <EASTL/hash_set.h>
#include <EASTL/map.h>
#include <EASTL/memory.h>
#include <EASTL/set.h>
#include <EASTL/span.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/utility.h>
#include <EASTL/vector.h>
#include <EASTL/vector_map.h>
#include <EASTL/vector_set.h>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>

// The container types of the engine (Types/CLAUDE.md). All but the maps keyed by String are plain `using`: the same type as the eastl:: name.
namespace BECore {

    /// The hash, equality and order a key gets by default: those of the key, except for String, whose map looks up by StringView
    /// and const char* without making a String (StringHash, StringEqual, StringLess).
    template <typename Key>
    using DefaultHash = std::conditional_t<std::is_same_v<Key, String>, StringHash, eastl::hash<Key>>;
    template <typename Key>
    using DefaultEqual = std::conditional_t<std::is_same_v<Key, String>, StringEqual, eastl::equal_to<Key>>;
    template <typename Key>
    using DefaultLess = std::conditional_t<std::is_same_v<Key, String>, StringLess, eastl::less<Key>>;

    template <typename T>
    using Vector = eastl::vector<T>;

    /// A vector whose memory comes from an arena (Memory/CLAUDE.md, "ArenaAllocator"); include <BECore/Memory/ArenaAllocator.h> to use it.
    template <typename T>
    using ArenaVector = eastl::vector<T, ArenaAllocator>;

    /// The EASTL allocator of the frame arena (Memory/FrameAllocator.h); only declared here, like ArenaAllocator.
    class FrameAllocator;

    /// A vector whose memory comes from the calling thread's frame arena and is given back after the present (Memory/CLAUDE.md, "Frame arena");
    /// include <BECore/Memory/FrameAllocator.h> to use it.
    template <typename T>
    using FrameVector = eastl::vector<T, FrameAllocator>;

    /// A vector with N elements inside the object that moves to the heap when it grows past N.
    template <typename T, size_t N>
    using SmallVector = eastl::fixed_vector<T, N, true>;

    /// A vector with room for exactly N elements, all inside the object; it never allocates and never grows past N.
    template <typename T, size_t N>
    using FixedVector = eastl::fixed_vector<T, N, false>;

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

    /// A map kept as a sorted vector (eastl::vector_map): lookup is a binary search, insertion moves elements (Types/CLAUDE.md says when to pick it).
    template <typename Key, typename T, typename Compare = DefaultLess<Key>>
    using FlatMap = eastl::vector_map<Key, T, Compare>;

    /// The set kept as a sorted vector (eastl::vector_set), like FlatMap.
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

    /// Structured bindings work (`auto [a, b] = pair`), `std::tie(a, b) = pair` does not.
    template <typename A, typename B>
    using Pair = eastl::pair<A, B>;

    template <typename A, typename B>
    [[nodiscard]] constexpr auto MakePair(A&& first, B&& second) {
        return eastl::make_pair(eastl::forward<A>(first), eastl::forward<B>(second));
    }

    /// std::variant, like Optional: the standard one, so `std::visit`, `std::get` and the rest stay spelled `std::`.
    template <typename... Ts>
    using Variant = std::variant<Ts...>;

    /// std::tuple: the reflection traits store fields in one, so `std::apply` and `std::get` stay spelled `std::`.
    template <typename... Ts>
    using Tuple = std::tuple<Ts...>;

}  // namespace BECore
