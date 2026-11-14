#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <EASTL/optional.h>

namespace BECore {

    // =========================================================================
    // Reflected test data: one field of every alias of the type layer, and the same fields spelled with the eastl:: / std:: names.
    // The .gen.hpp is generated from this header and included in ReflectionAliasesTest.cpp only.
    // =========================================================================

    namespace AliasTestData {

        inline constexpr int kFixedTextLength = 16;

        /// The container types whose fields the inspector does not edit (the property visitor has no general support for a container
        /// of values: `PropVisit::Visit` finds none), so these tests give the generated `AcceptPropertyVisitor` something to call
        template <typename T>
        inline constexpr bool kContainerField = false;
        template <typename T, typename Alloc>
        inline constexpr bool kContainerField<eastl::vector<T, Alloc>> = true;
        template <typename T, size_t N, bool Overflow, typename Alloc>
        inline constexpr bool kContainerField<eastl::fixed_vector<T, N, Overflow, Alloc>> = true;
        template <typename T, size_t N>
        inline constexpr bool kContainerField<eastl::array<T, N>> = true;
        template <typename K, typename V, typename H, typename E, typename Alloc, bool Cache>
        inline constexpr bool kContainerField<eastl::hash_map<K, V, H, E, Alloc, Cache>> = true;
        template <typename K, typename V, size_t N, size_t B, bool Overflow, typename H, typename E, bool Cache, typename Alloc>
        inline constexpr bool kContainerField<eastl::fixed_hash_map<K, V, N, B, Overflow, H, E, Cache, Alloc>> = true;
        template <typename K, typename V, typename C, typename Alloc>
        inline constexpr bool kContainerField<eastl::map<K, V, C, Alloc>> = true;
        template <typename A, typename B>
        inline constexpr bool kContainerField<eastl::pair<A, B>> = true;
        template <typename T>
        inline constexpr bool kContainerField<std::optional<T>> = true;
        template <typename T>
        inline constexpr bool kContainerField<eastl::optional<T>> = true;

    }  // namespace AliasTestData

    namespace PropVisit {

        template <typename T>
        requires AliasTestData::kContainerField<T> bool Visit(IPropertyVisitor& /*v*/, T& /*value*/, eastl::string_view /*key*/) {
            return false;
        }

    }  // namespace PropVisit

    namespace AliasTestData {

        /// Every alias, spelled with the alias
        struct AliasState {
            BE_CLASS(AliasState)

            BE_REFLECT_FIELD String text;
            BE_REFLECT_FIELD FixedString<kFixedTextLength> fixedText;
            BE_REFLECT_FIELD Vector<int32_t> list;
            BE_REFLECT_FIELD Vector<String> words;
            BE_REFLECT_FIELD SmallVector<int32_t, 4> small;
            BE_REFLECT_FIELD FixedVector<int32_t, 4> fixed;
            BE_REFLECT_FIELD Array<int32_t, 3> triple{};
            BE_REFLECT_FIELD HashMap<String, int32_t> hashed;
            BE_REFLECT_FIELD Map<String, int32_t> ordered;
            BE_REFLECT_FIELD FixedHashMap<String, int32_t, 8> fixedHashed;
            BE_REFLECT_FIELD Optional<int32_t> present;
            BE_REFLECT_FIELD Optional<int32_t> absent;
            BE_REFLECT_FIELD Pair<int32_t, String> entry;
        };

        /// The same fields spelled the way the engine wrote them before the aliases
        struct EastlState {
            BE_CLASS(EastlState)

            BE_REFLECT_FIELD eastl::string text;
            BE_REFLECT_FIELD eastl::fixed_string<char, kFixedTextLength, true> fixedText;
            BE_REFLECT_FIELD eastl::vector<int32_t> list;
            BE_REFLECT_FIELD eastl::vector<eastl::string> words;
            BE_REFLECT_FIELD eastl::fixed_vector<int32_t, 4, true> small;
            BE_REFLECT_FIELD eastl::fixed_vector<int32_t, 4, false> fixed;
            BE_REFLECT_FIELD eastl::array<int32_t, 3> triple{};
            BE_REFLECT_FIELD eastl::hash_map<eastl::string, int32_t> hashed;
            BE_REFLECT_FIELD eastl::map<eastl::string, int32_t> ordered;
            BE_REFLECT_FIELD eastl::fixed_hash_map<eastl::string, int32_t, 8> fixedHashed;
            BE_REFLECT_FIELD eastl::optional<int32_t> present;
            BE_REFLECT_FIELD eastl::optional<int32_t> absent;
            BE_REFLECT_FIELD eastl::pair<int32_t, eastl::string> entry;
        };

    }  // namespace AliasTestData

}  // namespace BECore
