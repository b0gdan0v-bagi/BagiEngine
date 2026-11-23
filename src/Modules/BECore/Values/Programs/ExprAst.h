#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// What hostile content may ask of the front end; each is a load error that names the limit
    namespace ExprLimits {
        inline constexpr size_t kMaxTextBytes = 4096;
        inline constexpr size_t kMaxNesting = 64;
        /// Levels of nodes under a node (a chain `a + b + c` is as high as it is long, which no parenthesis shows): the checker, the lowering and the printer recurse by it, and
        /// their frames are the biggest under a sanitizer, so the limit is what keeps them inside the smallest stack of a secondary thread the engine runs on
        inline constexpr size_t kMaxTreeHeight = 32;
        inline constexpr size_t kMaxNodes = 512;
    }  // namespace ExprLimits

    /// The place of a node in `ExprAst::nodes`; a node's operands are always earlier nodes, so a walk from the first to the last is a post-order
    using ExprNodeId = uint32_t;

    CORE_ENUM(ExprNodeKind, uint8_t, Number, Bool, String, Name, Call, Member, Unary, Binary, Index)

    /**
     * @brief One node of the untyped syntax tree
     *
     * `Number` keeps its text (`text`) and whether it is a float literal; a sign is a `Unary` above it. `String` is `'text'` and keeps the text between the quotes. `Name` is a whole dotted path
     * (`caster.attack`) and `Call` a call of a dotted name (`min`, or `a.b` as a mistake the checker names); `Member` is `.x` on an expression that is not a name, `first` its base; `Index` is
     * `first[second]`. `column` is the 1-based byte of the node in the text: the operator for `Unary` and `Binary`, the dot for `Member`, the `[` for `Index`, the start of the rest.
     */
    struct ExprNode {
        ExprNodeKind kind = ExprNodeKind::Number;
        /// `Unary`: `Neg` or `Not`; `Binary`: the operator by its `ProgramOperation`
        ProgramOperation operation = ProgramOperation::Add;
        bool isFloat = false;
        bool boolValue = false;
        uint32_t column = 0;
        /// `Unary` and `Binary`: the left operand; `Member`: its base; `Index`: the indexed expression
        ExprNodeId first = 0;
        /// `Binary`: the right operand; `Index`: the index
        ExprNodeId second = 0;
        /// `Call`: the arguments are `ExprAst::arguments[firstArgument, firstArgument + argumentCount)`
        uint32_t firstArgument = 0;
        uint32_t argumentCount = 0;
        /// `Number`: the digits as written; `String`: the characters between the quotes; `Name`, `Call`: the dotted name; `Member`: the member
        ArenaString text;
    };

    /**
     * @brief The nodes of a parsed formula: by index, the arguments of calls in one list, the root last
     *
     * Every container is on the thread's scratch arena, so a tree belongs to the `ScratchScope` that is alive when it is made and must not outlive it, be stored in a longer-lived
     * object or cross threads (`Memory/CLAUDE.md`, "Scratch arena and scopes"). `text` is an `ArenaString` because a dotted path is joined by the parser: it is not always a slice of the source.
     */
    struct ExprAst {
        ArenaVector<ExprNode> nodes;
        ArenaVector<ExprNodeId> arguments;
        ExprNodeId root = 0;

        [[nodiscard]] const ExprNode& operator[](ExprNodeId id) const {
            return nodes[id];
        }
    };

    /// The text the operator is written with (`&&`, `-`), for the printer and the messages
    [[nodiscard]] StringView OperatorSymbol(ProgramOperation operation);

    /**
     * @brief The canonical text of a tree: one space around a binary operator, ", " between arguments, every operand of a binary operator that is itself one in parentheses
     *
     * Parsing the result gives the same tree, so it is what a program may print and a key for equal formulas; numbers are printed as they were written.
     */
    [[nodiscard]] String PrintExpression(const ExprAst& ast);

}  // namespace BECore
