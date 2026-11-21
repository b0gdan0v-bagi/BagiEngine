#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Values/Explain/ValueExplain.h>

namespace BECore {

    /// The words of a tooltip that a language owns: the pattern of `2 per level` and the name a person gives a field
    struct TooltipWords {
        /// `{0}` is the number and `{1}` the label of the field; replaced by hand, never through `Format`, because the text comes from a string table
        String perPattern = "{0} per {1}";
        /// The label of a context path (`ability.level` is `level`); empty or returning nothing keeps the path
        MoveOnlyFunction<Optional<String>(StringView path)> label;
    };

    /// What `{name}` of a template stands for
    struct TooltipBinding {
        StringView name;
        /// Valid for the call of `FormatTooltip`
        const ValueExplanation* explanation = nullptr;
    };

    /**
     * @brief Fills @p templateText with the values of @p bindings: `Deals {damage} damage` becomes `Deals 12 (10 + 2 per level) damage`
     *
     * `{name}` is the value, followed by its parts in parentheses when it is a `Program` whose parts differ from the value; `{name:value}` is the value only; `{{` and `}}` are
     * braces. A name no binding has stays as written, so a template's mistake is visible and never an empty hole; a binding with no value (a context that lacks a level) is `?`.
     *
     * The parts are the formula as the trace has it: a subtree of constants is its value, `k * field` (either order, `k` a number) is the words' `per` pattern (`2 per level`), another
     * operation is infix with the parentheses its precedence needs, and a function is `name(arguments)`. A formula too long for a tooltip shows its value only.
     */
    [[nodiscard]] String FormatTooltip(StringView templateText, Span<const TooltipBinding> bindings, const TooltipWords& words);

}  // namespace BECore
