#pragma once

#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// Where a build of a tree is: the path of the node being built, for the errors, and how deep it is, for the limit
    class CalcBuildState;

    /**
     * @brief A node of a formula written as a tree of objects (`<tree type="CalcBinary" op="Mul">...`), the base of the `Calc*` classes
     *
     * A node knows only how to add itself to an untyped `ExprAst`, as the parser does for the text form: `BuildFormulaText` prints that tree and the text goes through the same
     * parser, checker and lowering as an `expr`, so a tree has no rules of its own. One class per shape of the AST, not per operator. The `Calc` prefix is there because the type registry
     * knows a class by its simple name.
     */
    class CalcNode : public RefCounted {
        BE_CLASS(CalcNode, FACTORY_BASE, ELEMENT = node)

    public:
        ~CalcNode() override = default;

        /// Adds the node after its operands to @p ast and gives its id; the operands are built with `state.Build`, which counts levels and parts
        [[nodiscard]] virtual std::expected<ExprNodeId, ProgramError> Append(ExprAst& ast, CalcBuildState& state) const = 0;

    protected:
        CalcNode() = default;
    };

    /// The cursor of one `BuildFormulaText`: enforces `ExprLimits` while the tree is walked, so a hostile one stops at the limit instead of recursing to the end of it
    class CalcBuildState {
    public:
        /// An error that names the node by its path in the tree (`tree.left.args[2]`); no text exists yet, so the column is 0
        [[nodiscard]] std::unexpected<ProgramError> Fail(StringView message) const;

        /// Adds @p node to @p ast, or fails at the limit of parts
        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Add(ExprAst& ast, ExprNode node) const;

        /// Builds the child @p child of the node of type @p owner under the name @p key (`left`, `args[2]`): a null one is the error `<owner>: '<key>' is missing`
        [[nodiscard]] std::expected<ExprNodeId, ProgramError> Build(ExprAst& ast, const IntrusivePtrNonAtomic<CalcNode>& child, StringView owner, StringView key);

        /// The path of the node being built
        [[nodiscard]] const String& Path() const {
            return _path;
        }

    private:
        String _path = "tree";
        uint32_t _depth = 0;
    };

    /**
     * @brief The canonical text of a tree: builds the `ExprAst`, prints it with `PrintExpression`
     *
     * The text is the program's source and goes through the parser, which stays the validator of numbers, names and strings. The tree's own limits are enforced while it is walked: more
     * than `ExprLimits::kMaxNodes` parts, more than `kMaxTreeHeight` levels (the recursion stops there), a missing child, an operator that does not fit its node and a leaf whose text
     * would read as more than one token. Allocates; for load time.
     */
    [[nodiscard]] std::expected<String, ProgramError> BuildFormulaText(const CalcNode& root);

}  // namespace BECore
