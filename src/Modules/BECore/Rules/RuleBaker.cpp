#include "RuleBaker.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore {

    namespace {

        constexpr StringView kAxisSeparator = " x ";

        /// A rule for a message: `#3 [origin] 'text'`, the parts that exist.
        String DescribeRule(uint32_t number, const RuleSpec& rule) {
            String text = Format("#{}", number);
            if (!rule.origin.empty()) {
                AppendFormat(text, " [{}]", rule.origin);
            }
            if (!rule.text.empty()) {
                AppendFormat(text, " '{}'", rule.text);
            }
            return text;
        }

        /// The cell as the value names of its axes: `stone x Hit x blast`.
        String DescribeCell(Span<const RuleAxis> axes, Span<const uint32_t> strides, uint32_t cell) {
            String text;
            for (size_t a = 0; a < axes.size(); ++a) {
                if (a != 0) {
                    text.append(kAxisSeparator.data(), kAxisSeparator.size());
                }
                const uint32_t value = (cell / strides[a]) % axes[a].Count();
                const StringView name = axes[a].ValueName(value);
                text.append(name.data(), name.size());
            }
            return text;
        }

        /// A dominates B: not lower on any axis and higher on at least one.
        bool Dominates(const uint32_t* a, const uint32_t* b, size_t axisCount) {
            bool higher = false;
            for (size_t i = 0; i < axisCount; ++i) {
                if (a[i] < b[i]) {
                    return false;
                }
                higher = higher || a[i] > b[i];
            }
            return higher;
        }

        uint64_t PairKey(uint32_t ruleA, uint32_t ruleB) {
            return (static_cast<uint64_t>(ruleA) << 32) | ruleB;
        }

        RuleError MakeError(String message) {
            return RuleError{std::move(message), {}};
        }

        String ConflictMessage(Span<const RuleAxis> axes, Span<const uint32_t> strides, Span<const RuleSpec> rules, const Vector<RuleConflict>& conflicts) {
            String message = Format("{} rule conflicts (rules with different values win the same cell, with equal specificity or neither more specific than the other):", conflicts.size());
            const size_t listed = eastl::min(conflicts.size(), kMaxListedRuleConflicts);
            for (size_t i = 0; i < listed; ++i) {
                const RuleConflict& conflict = conflicts[i];
                AppendFormat(message, "\n  {} and {}: {} cells, the first is {}", DescribeRule(conflict.ruleA, rules[conflict.ruleA]), DescribeRule(conflict.ruleB, rules[conflict.ruleB]),
                             conflict.cellCount, DescribeCell(axes, strides, conflict.firstCell));
            }
            if (conflicts.size() > listed) {
                AppendFormat(message, "\n  and {} more", conflicts.size() - listed);
            }
            return message;
        }

    }  // namespace

    std::expected<RuleCells, RuleError> BakeRules(Span<const RuleAxis> axes, Span<const RuleSpec> rules) {
        const size_t axisCount = axes.size();
        if (axisCount == 0) {
            return std::unexpected(MakeError("a rule table needs at least one axis"));
        }

        // Row-major strides; the product is checked step by step so it cannot overflow (every factor is below 2^32 and the running
        // product is kept at or below kMaxRuleCells).
        Vector<uint32_t> strides(axisCount, 0);
        uint64_t cellCount = 1;
        for (size_t a = axisCount; a-- > 0;) {
            strides[a] = static_cast<uint32_t>(cellCount);
            cellCount *= axes[a].Count();
            if (cellCount > kMaxRuleCells) {
                return std::unexpected(MakeError(Format("the axes make more than {} cells (axis '{}' brings it over)", kMaxRuleCells, axes[a].Name())));
            }
        }

        // The specificity of every rule per axis, and the sum that orders the rules (a rule that dominates another has a larger sum).
        const size_t ruleCount = rules.size();
        Vector<uint32_t> specificity(ruleCount * axisCount, 0);
        Vector<uint64_t> sums(ruleCount, 0);
        for (size_t r = 0; r < ruleCount; ++r) {
            const RuleSpec& rule = rules[r];
            if (rule.selectors.size() != axisCount) {
                return std::unexpected(MakeError(Format("rule {}: {} selectors for a table of {} axes", DescribeRule(static_cast<uint32_t>(r), rule), rule.selectors.size(), axisCount)));
            }
            for (size_t a = 0; a < axisCount; ++a) {
                const RuleSelector& selector = rule.selectors[a];
                if (selector.Kind() == RuleSelectorKind::Id && selector.Value() >= axes[a].Count()) {
                    return std::unexpected(MakeError(
                        Format("rule {}: the id {} is out of range of axis '{}' ({} values)", DescribeRule(static_cast<uint32_t>(r), rule), selector.Value(), axes[a].Name(), axes[a].Count())));
                }
                specificity[r * axisCount + a] = selector.Specificity();
                sums[r] += selector.Specificity();
            }
        }

        // Rules in decreasing sum, ties by number. A rule is only ever dominated by a rule met before it, and never dominates one met
        // before it, so each cell keeps exactly the maximal rules of the rules seen so far. The maximal set of a cell is a property of
        // the set of rules, so the order among rules of one sum does not matter.
        Vector<uint32_t> order(ruleCount);
        for (size_t r = 0; r < ruleCount; ++r) {
            order[r] = static_cast<uint32_t>(r);
        }
        eastl::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return sums[a] != sums[b] ? sums[a] > sums[b] : a < b; });

        // The first maximal rule of a cell is in `head`; a cell with more than one keeps the others in `more`. All members are kept,
        // also those with the value of another: a later rule can be incomparable to one of them and dominated by another.
        Vector<uint32_t> head(static_cast<size_t>(cellCount), kNoRule);
        HashMap<uint32_t, Vector<uint32_t>> more;

        Vector<Vector<uint32_t>> expansion(axisCount);
        Vector<size_t> position(axisCount);
        for (const uint32_t r : order) {
            const RuleSpec& rule = rules[r];
            const uint32_t* const own = &specificity[r * axisCount];
            bool matchesSomething = true;
            for (size_t a = 0; a < axisCount; ++a) {
                expansion[a] = axes[a].Expand(rule.selectors[a]);
                matchesSomething = matchesSomething && !expansion[a].empty();
            }
            if (!matchesSomething) {
                continue;
            }

            for (size_t a = 0; a < axisCount; ++a) {
                position[a] = 0;
            }
            while (true) {
                uint32_t cell = 0;
                for (size_t a = 0; a < axisCount; ++a) {
                    cell += expansion[a][position[a]] * strides[a];
                }

                if (head[cell] == kNoRule) {
                    head[cell] = r;
                } else {
                    bool dominated = Dominates(&specificity[static_cast<size_t>(head[cell]) * axisCount], own, axisCount);
                    Vector<uint32_t>* others = nullptr;
                    if (!dominated && !more.empty()) {
                        const auto found = more.find(cell);
                        if (found != more.end()) {
                            others = &found->second;
                            for (size_t i = 0; i < others->size() && !dominated; ++i) {
                                dominated = Dominates(&specificity[static_cast<size_t>((*others)[i]) * axisCount], own, axisCount);
                            }
                        }
                    }
                    if (!dominated) {
                        if (others != nullptr) {
                            others->push_back(r);
                        } else {
                            more[cell].push_back(r);
                        }
                    }
                }

                size_t a = axisCount;
                while (a-- > 0) {
                    if (++position[a] < expansion[a].size()) {
                        break;
                    }
                    position[a] = 0;
                }
                if (a == static_cast<size_t>(-1)) {
                    break;
                }
            }
        }

        // A cell with one maximal rule is won by it. A cell with several is won by the first declared when they agree on the value;
        // else every pair that disagrees is a conflict. The cells are visited in ascending order so the first cell of a pair is the
        // smallest.
        Vector<uint32_t> multi;
        multi.reserve(more.size());
        for (const auto& entry : more) {
            multi.push_back(entry.first);
        }
        eastl::sort(multi.begin(), multi.end());

        Vector<RuleConflict> conflicts;
        HashMap<uint64_t, uint32_t> conflictOfPair;
        Vector<uint32_t> members;
        for (const uint32_t cell : multi) {
            members.clear();
            members.push_back(head[cell]);
            for (const uint32_t other : more[cell]) {
                members.push_back(other);
            }
            eastl::sort(members.begin(), members.end());
            for (size_t i = 0; i < members.size(); ++i) {
                for (size_t j = i + 1; j < members.size(); ++j) {
                    if (rules[members[i]].valueClass == rules[members[j]].valueClass) {
                        continue;
                    }
                    const auto [it, inserted] = conflictOfPair.emplace(PairKey(members[i], members[j]), static_cast<uint32_t>(conflicts.size()));
                    if (inserted) {
                        conflicts.push_back(RuleConflict{members[i], members[j], cell, 1});
                    } else {
                        ++conflicts[it->second].cellCount;
                    }
                }
            }
            head[cell] = members.front();
        }

        if (!conflicts.empty()) {
            eastl::sort(conflicts.begin(), conflicts.end(), [](const RuleConflict& a, const RuleConflict& b) { return a.ruleA != b.ruleA ? a.ruleA < b.ruleA : a.ruleB < b.ruleB; });
            RuleError error;
            error.message = ConflictMessage(axes, strides, rules, conflicts);
            error.conflicts = std::move(conflicts);
            return std::unexpected(std::move(error));
        }

        RuleCells cells;
        cells.axisSizes.reserve(axisCount);
        for (const RuleAxis& axis : axes) {
            cells.axisSizes.push_back(axis.Count());
        }
        cells.wonCells.assign(ruleCount, 0);
        for (const uint32_t winner : head) {
            if (winner != kNoRule) {
                ++cells.wonCells[winner];
            }
        }
        cells.cellRule = std::move(head);
        return cells;
    }

}  // namespace BECore
