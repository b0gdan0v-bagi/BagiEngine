#include "CalcNode.h"

#include <Generated/CalcNode.gen.hpp>

namespace BECore {

    std::unexpected<ProgramError> CalcBuildState::Fail(StringView message) const {
        return std::unexpected(ProgramError{Format("{} (at {})", message, _path), 0});
    }

    std::expected<ExprNodeId, ProgramError> CalcBuildState::Add(ExprAst& ast, ExprNode node) const {
        if (ast.nodes.size() >= ExprLimits::kMaxNodes) {
            return Fail(Format("the tree has more than {} parts", ExprLimits::kMaxNodes));
        }
        ast.nodes.push_back(std::move(node));
        return static_cast<ExprNodeId>(ast.nodes.size() - 1);
    }

    std::expected<ExprNodeId, ProgramError> CalcBuildState::Build(ExprAst& ast, const IntrusivePtr<CalcNode>& child, StringView owner, StringView key) {
        // The path is the owner's: the message names the missing child by its key
        if (!child) {
            return Fail(Format("{}: '{}' is missing", owner, key));
        }
        const size_t mark = _path.size();
        _path += '.';
        _path.append(key.data(), key.size());
        // The levels of the tree are the height of the AST it builds (one node per object), so this is the limit of the parser, and it stops the recursion before the tree's end
        if (_depth >= ExprLimits::kMaxTreeHeight) {
            auto error = Fail(Format("the tree is more than {} levels high", ExprLimits::kMaxTreeHeight));
            _path.resize(mark);
            return error;
        }
        ++_depth;
        auto id = child->Append(ast, *this);
        --_depth;
        _path.resize(mark);
        return id;
    }

    std::expected<String, ProgramError> BuildFormulaText(const CalcNode& root) {
        const ScratchScope scratch;
        ExprAst ast;
        CalcBuildState state;
        const auto id = root.Append(ast, state);
        if (!id.has_value()) {
            return std::unexpected(id.error());
        }
        ast.root = *id;
        return PrintExpression(ast);
    }

}  // namespace BECore
