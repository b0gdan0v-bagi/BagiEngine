#include "RuleReport.h"

namespace BECore {

    namespace {

        constexpr StringView kAxisSeparator = " x ";
        constexpr StringView kColumnSeparator = " | ";

        void AppendEscaped(String& out, StringView text) {
            for (const char c : text) {
                if (c == '\n') {
                    out.append("\\n");
                } else {
                    out.push_back(c);
                }
            }
        }

        void AppendValueName(String& out, const RuleAxis& axis, uint32_t value) {
            const StringView name = axis.ValueName(value);
            out.append(name.data(), name.size());
        }

        /// The value of each axis of a row-major cell.
        Vector<uint32_t> CellValues(const RuleCells& cells, uint32_t cell) {
            Vector<uint32_t> values(cells.axisSizes.size());
            for (size_t a = cells.axisSizes.size(); a-- > 0;) {
                values[a] = cell % cells.axisSizes[a];
                cell /= cells.axisSizes[a];
            }
            return values;
        }

        RuleRef MakeRef(uint32_t number, const RuleSpec& rule) {
            return RuleRef{number, rule.origin, rule.text};
        }

        /// `#3 [origin] text`, the parts that exist.
        void AppendRef(String& out, const RuleRef& ref) {
            AppendFormat(out, "#{}", ref.rule);
            if (!ref.origin.empty()) {
                AppendFormat(out, " [{}]", ref.origin);
            }
            if (!ref.text.empty()) {
                AppendFormat(out, " {}", ref.text);
            }
        }

    }  // namespace

    CellExplanation ExplainRuleCell(Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, uint32_t cell) {
        ASSERT(cell < cells.cellRule.size(), "ExplainRuleCell: cell out of range");
        const Vector<uint32_t> values = CellValues(cells, cell);
        const uint32_t winner = cells.cellRule[cell];
        CellExplanation explanation;
        for (uint32_t r = 0; r < rules.size(); ++r) {
            bool matches = true;
            for (size_t a = 0; a < axes.size() && matches; ++a) {
                matches = axes[a].Matches(rules[r].selectors[a], values[a]);
            }
            if (!matches) {
                continue;
            }
            if (r == winner) {
                explanation.winner = MakeRef(r, rules[r]);
            } else {
                explanation.overridden.push_back(MakeRef(r, rules[r]));
            }
        }
        return explanation;
    }

    String ExplainRuleCellText(Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, uint32_t cell, StringView valueText) {
        const CellExplanation explanation = ExplainRuleCell(axes, rules, cells, cell);
        const Vector<uint32_t> values = CellValues(cells, cell);
        String line;
        for (size_t a = 0; a < axes.size(); ++a) {
            if (a != 0) {
                line.append(kAxisSeparator.data(), kAxisSeparator.size());
            }
            AppendValueName(line, axes[a], values[a]);
        }
        line.append(" = ");
        AppendEscaped(line, valueText);
        line.append("  <- ");
        if (!explanation.winner) {
            line.append("default");
            return line;
        }
        AppendRef(line, *explanation.winner);
        if (!explanation.overridden.empty()) {
            line.append("; overridden: ");
            for (size_t i = 0; i < explanation.overridden.size(); ++i) {
                if (i != 0) {
                    line.append(", ");
                }
                AppendRef(line, explanation.overridden[i]);
            }
        }
        return line;
    }

    String ExportRuleCells(StringView name, Span<const RuleAxis> axes, Span<const RuleSpec> rules, const RuleCells& cells, Span<const String> classTexts, StringView defaultText,
                           const RuleExportOptions& options) {
        String out;
        AppendFormat(out, "# rules {}: ", name);
        for (size_t a = 0; a < axes.size(); ++a) {
            if (a != 0) {
                out.append(kAxisSeparator.data(), kAxisSeparator.size());
            }
            AppendFormat(out, "{}[{}]", axes[a].Name(), axes[a].Count());
        }
        out.append("\n# default: ");
        AppendEscaped(out, defaultText);
        out.push_back('\n');

        // The values of the cell advance like an odometer, the last axis fastest, so no division per cell.
        Vector<uint32_t> values(axes.size(), 0);
        for (const uint32_t rule : cells.cellRule) {
            if (rule != kNoRule) {
                for (size_t a = 0; a < axes.size(); ++a) {
                    if (a != 0) {
                        out.append(kColumnSeparator.data(), kColumnSeparator.size());
                    }
                    AppendValueName(out, axes[a], values[a]);
                }
                out.append(" = ");
                AppendEscaped(out, classTexts[rules[rule].valueClass]);
                if (options.withRules) {
                    AppendFormat(out, " <- #{}", rule);
                    if (!rules[rule].origin.empty()) {
                        AppendFormat(out, " {}", rules[rule].origin);
                    }
                }
                out.push_back('\n');
            }
            for (size_t a = axes.size(); a-- > 0;) {
                if (++values[a] < axes[a].Count()) {
                    break;
                }
                values[a] = 0;
            }
        }
        return out;
    }

}  // namespace BECore
