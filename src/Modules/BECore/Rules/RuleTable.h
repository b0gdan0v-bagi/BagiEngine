#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Format/Format.h>
#include <BECore/Rules/RuleAxis.h>
#include <BECore/Rules/RuleBaker.h>
#include <BECore/Types/Types.h>
#include <concepts>
#include <cstdint>
#include <expected>
#include <initializer_list>

namespace BECore {

    /// The value of a rule table cell: copyable and comparable (rules with equal values share a value class).
    template <typename T>
    concept RuleValue = std::copy_constructible<T> && std::equality_comparable<T>;

    /**
     * @brief A table baked from selector rules: one value per cell of the axes, found by one index into a dense vector.
     *
     * `Lookup(ids...)` takes one id per axis and does no search, no hash and no branch on the kind of rule; the ids are checked
     * (`ASSERT`) for their number and their range. Made by `RuleTable::Bake`.
     */
    template <RuleValue Value>
    class BakedRuleTable {
    public:
        /// The value of the cell: what the winning rule says, the default value when no rule matches.
        template <typename... Ids>
        requires(sizeof...(Ids) > 0 && (std::convertible_to<Ids, uint32_t> && ...)) [[nodiscard]] const Value& Lookup(Ids... ids) const {
            return _cellValues[CellIndex(ids...)];
        }

        [[nodiscard]] const Value& LookupCell(uint32_t cell) const {
            ASSERT(cell < _cellValues.size(), "BakedRuleTable::LookupCell: cell out of range");
            return _cellValues[cell];
        }

        /// The number of the rule that wins the cell (the position of its AddRule call), empty when no rule matches.
        template <typename... Ids>
        requires(sizeof...(Ids) > 0 && (std::convertible_to<Ids, uint32_t> && ...)) [[nodiscard]] Optional<uint32_t> Winner(Ids... ids) const {
            const uint32_t rule = _cells.cellRule[CellIndex(ids...)];
            return rule == kNoRule ? Optional<uint32_t>{} : Optional<uint32_t>{rule};
        }

        /// The row-major index of the cell: the last axis varies fastest.
        template <typename... Ids>
        requires(sizeof...(Ids) > 0 && (std::convertible_to<Ids, uint32_t> && ...)) [[nodiscard]] uint32_t CellIndex(Ids... ids) const {
            ASSERT(sizeof...(Ids) == _axes.size(), "BakedRuleTable: one id per axis");
            const uint32_t values[] = {static_cast<uint32_t>(ids)...};
            uint32_t cell = 0;
            for (size_t a = 0; a < sizeof...(Ids); ++a) {
                ASSERT(values[a] < _cells.axisSizes[a], "BakedRuleTable: id out of range of its axis");
                cell = cell * _cells.axisSizes[a] + values[a];
            }
            return cell;
        }

        [[nodiscard]] Span<const RuleAxis> Axes() const {
            return Span<const RuleAxis>(_axes.data(), _axes.size());
        }

        [[nodiscard]] uint32_t RuleCount() const {
            return static_cast<uint32_t>(_rules.size());
        }

        [[nodiscard]] uint32_t CellCount() const {
            return static_cast<uint32_t>(_cellValues.size());
        }

        /// The number of cells the rule wins.
        [[nodiscard]] uint32_t WonCells(uint32_t rule) const {
            ASSERT(rule < _cells.wonCells.size(), "BakedRuleTable::WonCells: rule out of range");
            return _cells.wonCells[rule];
        }

        [[nodiscard]] const Value& DefaultValue() const {
            return _defaultValue;
        }

    private:
        template <RuleValue>
        friend class RuleTable;

        BakedRuleTable(Vector<RuleAxis> axes, Vector<RuleSpec> rules, RuleCells cells, Vector<Value> cellValues, Value defaultValue)
            : _axes(std::move(axes)), _rules(std::move(rules)), _cells(std::move(cells)), _cellValues(std::move(cellValues)), _defaultValue(std::move(defaultValue)) {}

        Vector<RuleAxis> _axes;
        Vector<RuleSpec> _rules;
        RuleCells _cells;
        Vector<Value> _cellValues;
        Value _defaultValue;
    };

    /**
     * @brief Rules of a table over named axes, in the order they are declared, baked into a BakedRuleTable.
     *
     * A rule is one selector text per axis (`*`, a value name, `tag:<path>`, `tag:<path>.*`: RuleAxis) and a value. The rule
     * numbers are the order of declaration and are what every message and `Winner` speak. Which rule wins a cell and when two
     * rules conflict: BakeRules.
     */
    template <RuleValue Value>
    class RuleTable {
    public:
        explicit RuleTable(Vector<RuleAxis> axes) : _axes(std::move(axes)) {}

        /// Adds a rule and returns its number. An error (a selector that does not parse, a wrong number of selectors) names @p origin
        /// and adds nothing.
        [[nodiscard]] std::expected<uint32_t, RuleError> AddRule(Span<const StringView> selectors, Value value, String origin) {
            auto spec = ParseRuleSpec(Axes(), selectors, origin);
            if (!spec) {
                return std::unexpected(std::move(spec.error()));
            }
            const uint32_t number = static_cast<uint32_t>(_rules.size());
            Push(std::move(*spec), std::move(value));
            return number;
        }

        [[nodiscard]] std::expected<uint32_t, RuleError> AddRule(std::initializer_list<StringView> selectors, Value value, String origin) {
            return AddRule(Span<const StringView>(selectors.begin(), selectors.size()), std::move(value), std::move(origin));
        }

        /**
         * @brief Adds the rule and its mirror (the selectors of the first two axes swapped) with the same value.
         *
         * For tables whose first two axes have the same domain (`HaveSameDomain`; an error otherwise). Returns the number of the rule;
         * the mirror is the next one and its text ends with `(mirrored)`. A rule whose first two selectors are equal is its own
         * mirror and is added once. Adds nothing on an error.
         */
        [[nodiscard]] std::expected<uint32_t, RuleError> AddSymmetric(Span<const StringView> selectors, Value value, String origin) {
            if (_axes.size() < 2 || !HaveSameDomain(_axes[0], _axes[1])) {
                return std::unexpected(RuleError{Format("rule [{}]: AddSymmetric needs a table whose first two axes have the same values", origin), {}});
            }
            auto spec = ParseRuleSpec(Axes(), selectors, origin);
            if (!spec) {
                return std::unexpected(std::move(spec.error()));
            }
            Vector<StringView> swapped(selectors.begin(), selectors.end());
            eastl::swap(swapped[0], swapped[1]);
            auto mirror = ParseRuleSpec(Axes(), Span<const StringView>(swapped.data(), swapped.size()), origin);
            if (!mirror) {
                return std::unexpected(std::move(mirror.error()));
            }
            const uint32_t number = static_cast<uint32_t>(_rules.size());
            const bool ownMirror = spec->selectors[0] == spec->selectors[1];
            Push(std::move(*spec), value);
            if (!ownMirror) {
                mirror->text.append(" (mirrored)");
                Push(std::move(*mirror), std::move(value));
            }
            return number;
        }

        /// The two selectors of the first two axes, and `*` on the others.
        [[nodiscard]] std::expected<uint32_t, RuleError> AddSymmetric(StringView first, StringView second, Value value, String origin) {
            Vector<StringView> selectors(_axes.size(), StringView("*"));
            if (_axes.size() >= 2) {
                selectors[0] = first;
                selectors[1] = second;
            }
            return AddSymmetric(Span<const StringView>(selectors.data(), selectors.size()), std::move(value), std::move(origin));
        }

        [[nodiscard]] uint32_t RuleCount() const {
            return static_cast<uint32_t>(_rules.size());
        }

        [[nodiscard]] Span<const RuleAxis> Axes() const {
            return Span<const RuleAxis>(_axes.data(), _axes.size());
        }

        /// Decides every cell; @p defaultValue is the value of a cell no rule matches. The table can be changed and baked again.
        [[nodiscard]] std::expected<BakedRuleTable<Value>, RuleError> Bake(Value defaultValue) const {
            auto cells = BakeRules(Axes(), Span<const RuleSpec>(_rules.data(), _rules.size()));
            if (!cells) {
                return std::unexpected(std::move(cells.error()));
            }
            Vector<Value> cellValues;
            cellValues.reserve(cells->cellRule.size());
            for (const uint32_t rule : cells->cellRule) {
                cellValues.push_back(rule == kNoRule ? defaultValue : _values[_rules[rule].valueClass]);
            }
            return BakedRuleTable<Value>(_axes, _rules, std::move(*cells), std::move(cellValues), std::move(defaultValue));
        }

    private:
        /// Gives the rule the class of its value (an equal value of an earlier rule shares it) and keeps it.
        void Push(RuleSpec spec, Value value) {
            uint32_t valueClass = 0;
            while (valueClass < _values.size() && !(_values[valueClass] == value)) {
                ++valueClass;
            }
            if (valueClass == _values.size()) {
                _values.push_back(std::move(value));
            }
            spec.valueClass = valueClass;
            _rules.push_back(std::move(spec));
        }

        Vector<RuleAxis> _axes;
        Vector<RuleSpec> _rules;
        /// One entry per value class.
        Vector<Value> _values;
    };

}  // namespace BECore
