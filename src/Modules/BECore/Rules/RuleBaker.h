#pragma once

#include <BECore/Rules/RuleAxis.h>
#include <BECore/Types/Types.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The most cells a baked table holds (the product of the axis sizes).
    inline constexpr uint64_t kMaxRuleCells = uint64_t{1} << 24;

    /// `RuleCells::cellRule` of a cell no rule matches.
    inline constexpr uint32_t kNoRule = UINT32_MAX;

    /// The most conflicts `RuleError::message` lists; `RuleError::conflicts` has all of them.
    inline constexpr size_t kMaxListedRuleConflicts = 16;

    /// One rule as the baker sees it: a selector per axis, the class of its value (rules with equal values share a class) and,
    /// for the messages, where it came from and its text.
    struct RuleSpec {
        Vector<RuleSelector> selectors;
        uint32_t valueClass = 0;
        String origin;
        String text;
    };

    /// Two rules that both win the same cells and disagree about the value: `ruleA < ruleB` are rule numbers (positions in the
    /// span given to BakeRules); `firstCell` is the first such cell in row-major order, `cellCount` how many cells they clash on.
    struct RuleConflict {
        uint32_t ruleA = 0;
        uint32_t ruleB = 0;
        uint32_t firstCell = 0;
        uint32_t cellCount = 0;
    };

    /// Why a table did not bake: a sentence with the rules and the cells (at most kMaxListedRuleConflicts of the conflicts, then
    /// `and N more`), and all the conflicts as data.
    struct RuleError {
        String message;
        Vector<RuleConflict> conflicts;
    };

    /// The baked cells: `cellRule` is row-major over the axes in order (the last axis varies fastest), `axisSizes` the sizes;
    /// `wonCells[rule]` is the number of cells the rule wins (0 for a rule that is shadowed everywhere).
    struct RuleCells {
        Vector<uint32_t> axisSizes;
        Vector<uint32_t> cellRule;
        Vector<uint32_t> wonCells;
    };

    /// Reads one selector text per axis (`RuleAxis::ParseSelector`) into a rule with `valueClass` 0 and the text
    /// `<axis>=<selector> <axis>=<selector> ...`. A wrong number of texts or a text that does not parse is an error that names @p origin.
    [[nodiscard]] std::expected<RuleSpec, RuleError> ParseRuleSpec(Span<const RuleAxis> axes, Span<const StringView> selectors, StringView origin);

    /// Whether two axes have the same values: the same count, names and tag sets (a rule on `(a, b)` can be mirrored to `(b, a)`).
    [[nodiscard]] bool HaveSameDomain(const RuleAxis& a, const RuleAxis& b);

    /**
     * @brief Decides, for every cell of the axes, which rule wins it.
     *
     * A rule matches a cell when each of its selectors matches the cell's value of that axis. Rule A *dominates* B when A's
     * specificity is >= B's on every axis and > on at least one. The rules that match a cell are reduced to the maximal ones (no
     * other matching rule dominates them); if they all have the same `valueClass` the cell is won by the first declared of them
     * (the smallest rule number), otherwise the table does not bake and every pair of maximal rules with different classes is a
     * RuleConflict (equal specificity, or two rules neither of which dominates the other). The result never depends on the order
     * of @p rules except through the rule numbers.
     *
     * Errors besides conflicts: no axes, a rule with a wrong number of selectors or an Id out of range, a product of the axis
     * sizes above kMaxRuleCells.
     */
    [[nodiscard]] std::expected<RuleCells, RuleError> BakeRules(Span<const RuleAxis> axes, Span<const RuleSpec> rules);

}  // namespace BECore
