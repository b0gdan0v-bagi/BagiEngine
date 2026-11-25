#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Widgets/PanelAssets/PanelBinding.h>
#include <BECore/Widgets/PanelAssets/PanelBindingReader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// How a term compares its bound value; `Truth` is the plain `[!]source.path` of a bool field
    CORE_ENUM(PanelCompare, uint8_t, Truth, Equal, NotEqual, Less, LessEqual, Greater, GreaterEqual)

    /// One term of a condition. A compare term holds its literal as read once at load: a number (`numeric`) or a word
    struct PanelConditionTerm {
        PanelBinding binding;
        PanelCompare compare = PanelCompare::Truth;
        bool numeric = false;
        double number = 0.0;
        String word;
    };

    /**
     * @brief A parsed condition: an or-list of and-lists of terms
     *
     * An empty condition (no alternative) is always true, as an empty attribute is.
     */
    struct PanelCondition {
        Vector<Vector<PanelConditionTerm>> alternatives;
    };

    /**
     * @brief Reads `term ( and term )* ( or ... )*`
     *
     * `and` binds tighter than `or`, there are no parentheses. A term is `[!]source.path` or `source.path <op> literal` with `<op>` one of
     * `== != < <= > >=`; the literal is a number or a bare word (an enum name, `true`, `false`), and an order compare takes a number only.
     * Tokens are separated by blanks, which a quoted key of a path may hold. At most `PanelAssetLimits::kMaxConditionTerms` terms. The error
     * is a short line; client text: nothing is interned, no input asserts.
     */
    [[nodiscard]] std::expected<PanelCondition, String> ParsePanelCondition(StringView text);

    /**
     * @brief Evaluates a condition against the sources
     *
     * A number term reads through ReadNumber, a word term through ReadText into @p scratch; a term that cannot be read (any status but `Ok`,
     * a word under an order compare) counts as @p whenUnreadable. Allocates nothing once the paths are compiled and @p scratch is grown.
     */
    [[nodiscard]] bool EvaluatePanelCondition(PanelBindingReader& reader, const PanelDataSources& sources, const PanelCondition& condition, bool whenUnreadable, String& scratch);

}  // namespace BECore
