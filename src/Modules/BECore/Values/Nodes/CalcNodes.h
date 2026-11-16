#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Nodes/CalcNode.h>
#include <BECore/Values/Programs/Opcode.h>

namespace BECore {

    // One class per shape of the AST (`ExprNodeKind`), not per operator: the operator is a field. Each prints, through `BuildFormulaText`, the text the parser reads back into the same tree.
    // A leaf's text is checked to read as the one token it stands for, so a field cannot smuggle an operator into the printed text.

    /// A number as written: `12`, `1.5`, `1e3` (no sign: a sign is a `CalcUnary`)
    class CalcNumber : public CalcNode {
        BE_CLASS(CalcNumber)

    public:
        CalcNumber() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD String text;
    };

    /// `true` or `false`
    class CalcBool : public CalcNode {
        BE_CLASS(CalcBool)

    public:
        CalcBool() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD bool value = false;
    };

    /// A string literal (`'Fire'`): the characters between the quotes, which cannot hold a quote
    class CalcText : public CalcNode {
        BE_CLASS(CalcText)

    public:
        CalcText() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD String text;
    };

    /// A path of the context (`caster.attack`) or an enumerator (`Fire`), what a `Name` is in the text
    class CalcRead : public CalcNode {
        BE_CLASS(CalcRead)

    public:
        CalcRead() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD String path;
    };

    /// `-operand` (`Neg`) or `!operand` (`Not`)
    class CalcUnary : public CalcNode {
        BE_CLASS(CalcUnary)

    public:
        CalcUnary() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD ProgramOperation op = ProgramOperation::Neg;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<CalcNode> operand;
    };

    /// `left op right` for `Add Sub Mul Div Mod Eq Ne Lt Le Gt Ge And Or`
    class CalcBinary : public CalcNode {
        BE_CLASS(CalcBinary)

    public:
        CalcBinary() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD ProgramOperation op = ProgramOperation::Add;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<CalcNode> left;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<CalcNode> right;
    };

    /// `function(args...)`: `min`, `lerp`, `select`, `vec3`, `at` (an inline table, the index first) and every other function of the language
    class CalcCall : public CalcNode {
        BE_CLASS(CalcCall)

    public:
        CalcCall() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD String function;
        /// The elements are written `<node type="...">`, the element name of the base
        BE_REFLECT_FIELD Vector<IntrusivePtrNonAtomic<CalcNode>> args;
    };

    /// `base.name` for a base that is no bare path (`lerp(a, b, t).x`)
    class CalcMember : public CalcNode {
        BE_CLASS(CalcMember)

    public:
        CalcMember() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD IntrusivePtrNonAtomic<CalcNode> base;
        BE_REFLECT_FIELD String name;
    };

    /// `ByLevel('table', index)`: an entry of the named table the game supplies (`Values/CLAUDE.md`, "Named tables")
    class CalcByLevel : public CalcNode {
        BE_CLASS(CalcByLevel)

    public:
        CalcByLevel() = default;

        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const override;

        BE_REFLECT_FIELD String table;
        BE_REFLECT_FIELD IntrusivePtrNonAtomic<CalcNode> index;
    };

}  // namespace BECore
