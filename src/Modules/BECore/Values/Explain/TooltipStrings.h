#pragma once

#include <BECore/UI/UIStrings.h>
#include <BECore/Values/Explain/ValueTooltip.h>

namespace BECore {

    /// The key of the `per` pattern in the string table
    inline constexpr StringView kTooltipPerKey = "values.per";

    /// The prefix of the label of a context path: `values.label.ability.level`
    inline constexpr StringView kTooltipLabelPrefix = "values.label.";

    /**
     * @brief The words of a tooltip in the current language of @p strings: the pattern at `values.per` and the labels at `values.label.<path>`
     *
     * Reads through `UIStrings::Find`, so a key a language lacks falls back to the default language and a miss is not recorded. The words refer to @p strings, which must outlive them; a language
     * switch between two calls gives the other words. A new language is strings only.
     */
    [[nodiscard]] TooltipWords MakeTooltipWords(const UIStrings& strings);

}  // namespace BECore
