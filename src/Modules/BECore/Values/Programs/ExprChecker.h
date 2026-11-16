#pragma once

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/ExprAst.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <cstdint>
#include <expected>

namespace BECore {

    /// The place of a node in `TypedExpr::nodes`; an operand is always an earlier node
    using TypedNodeId = uint32_t;

    CORE_ENUM(TypedNodeKind, uint8_t, Constant, Slot, Operation)

    /**
     * @brief A node of a checked formula: it has a program type and is one of three things
     *
     * `Constant`: the register image of a literal already of its type. `Slot`: a field of a context object (`slot`, and the path as written). `Operation`: an opcode
     * applied to `operandCount` earlier nodes, a conversion included; `Select` has its condition first. Nothing is left untyped: a literal took the type of its place.
     */
    struct TypedNode {
        TypedNodeKind kind = TypedNodeKind::Constant;
        ProgramType type = ProgramType::Int32;
        Opcode opcode = Opcode::Invalid;
        uint8_t operandCount = 0;
        Array<TypedNodeId, 3> operands{};
        RegisterImage constant{};
        SlotRef slot{};
        String path;
        uint32_t column = 0;
    };

    /// A formula that passed the checker, in post-order: the root is the last node
    struct TypedExpr {
        Vector<TypedNode> nodes;
        TypedNodeId root = 0;
        ProgramType type = ProgramType::Int32;

        [[nodiscard]] const TypedNode& operator[](TypedNodeId id) const {
            return nodes[id];
        }
    };

    /**
     * @brief Types a parsed formula against the declared context and the type its field has
     *
     * Every name path is linked with `LinkSlot` (so its messages, `did you mean` and the list of fields come from there) and must name a field of a program type.
     * There is no implicit conversion: two operands of two types are an error that says so, and the result must be exactly @p expected. An integer literal takes the
     * type of the operand beside it, or @p expected (int, int64, float or double); a float literal is float or double and an error where an integer is needed. A
     * division or a modulo by a literal zero, an operator or a function a type lacks, a member (`.x`: no type has components yet), a wrong number of arguments and an
     * @p expected outside the program types are errors. An error is `{message, column of the node}`; the one about @p expected has column 0.
     */
    [[nodiscard]] std::expected<TypedExpr, ProgramError> CheckExpression(const ExprAst& ast, const ContextDecl& declaration, const TypeDesc& expected);

}  // namespace BECore
