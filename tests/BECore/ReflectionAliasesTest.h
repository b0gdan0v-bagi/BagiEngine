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
            BE_REFLECT_FIELD Variant<int32_t, String> choice;
        };

        /// The same fields spelled with the eastl:: / std:: names; no `choice`: `Variant` is `std::variant` itself,
        /// so there is no other spelling to compare
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
