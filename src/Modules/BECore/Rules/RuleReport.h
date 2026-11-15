#pragma once

#include <BECore/Rules/RuleAxis.h>
#include <BECore/Rules/RuleBaker.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    /// A rule as an explanation names it: its number (the position of its `AddRule` call), where it came from and its text
    /// (`kind=stone verb=Hit tag=*`, a mirrored rule ends with `(mirrored)`).
    struct RuleRef {
        uint32_t rule = 0;
        String origin;
        String text;
    };

    /// Why a cell holds its value: the rule that wins it (empty when no rule matches: the default value) and every other rule that
    /// matches it, ascending by number.
    struct CellExplanation {
        Optional<RuleRef> winner;
        Vector<RuleRef> overridden;
    };

    struct RuleExportOptions {
        /// Ends each cell line with ` <- #<rule> <origin>`. Off by default: the rule numbers move when a rule is added, and a golden
        /// file wants the table only.
        bool withRules = false;
    };

    /// The non-template part of BakedRuleTable::Explain: the rules that match @p cell (a row-major index of @p cells) and the winner.
    [[nodiscard]] CellExplanation ExplainRuleCell(Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, uint32_t cell);

    /// The one-line form: `stone x Hit x blast = <value>  <- #12 [<origin>] kind=stone verb=Hit tag=*; overridden: #3 [<origin>] ...`,
    /// or `<- default` for a cell no rule won. A newline in @p valueText is written as `\n`.
    [[nodiscard]] String ExplainRuleCellText(Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, uint32_t cell, StringView valueText);

    /**
     * @brief The golden-snapshot text of a baked table; the format is pinned in `Rules/CLAUDE.md`.
     *
     * @p classTexts is the text of each value class (`RuleSpec::valueClass`), @p defaultText the text of the default value. One line
     * per cell a rule won, in row-major order; the text depends on nothing but the axes, the rules' values and their winning cells.
     */
    [[nodiscard]] String ExportRuleCells(StringView name, Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, Span<const String> classTexts, StringView defaultText,
                                         const RuleExportOptions& options);

}  // namespace BECore
