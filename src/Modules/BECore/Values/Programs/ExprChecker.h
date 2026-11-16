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

    /// The most operands a node has: `vec4(a, b, c, d)`, `quat` and `color`
    inline constexpr size_t kMaxTypedOperands = 4;

    /**
     * @brief The type of a node of a formula: a program type and, for `Enum`, which enum
     *
     * The register image of every enum is the index of the enumerator, so the opcodes cannot tell two enums apart; the checker can, and two enum types are equal when their
     * descriptors are `SameType`. `enumeration` is the descriptor of a field or of the type asked for: static data of the reflection, so a pointer to it stays valid.
     */
    struct ExprType {
        ProgramType program = ProgramType::Int32;
        /// `Enum` only
        const TypeDesc* enumeration = nullptr;

        constexpr ExprType() = default;
        constexpr ExprType(ProgramType type) : program(type) {}

        [[nodiscard]] static ExprType OfEnum(const TypeDesc& descriptor) {
            ExprType type(ProgramType::Enum);
            type.enumeration = &descriptor;
            return type;
        }

        [[nodiscard]] bool operator==(const ExprType& other) const {
            if (program != other.program) {
                return false;
            }
            if (program != ProgramType::Enum || enumeration == nullptr || other.enumeration == nullptr) {
                return program != ProgramType::Enum || enumeration == other.enumeration;
            }
            return SameType(*enumeration, *other.enumeration);
        }
    };

    /**
     * @brief A node of a checked formula: it has a program type and is one of three things
     *
     * `Constant`: the register image of a literal already of its type. `Slot`: a field of a context object (`slot`, and the path as written). `Operation`: an opcode
     * applied to `operandCount` earlier nodes, a conversion included; `Select` has its condition first, a mixed operation (`vec3 * float`) its left operand first. Nothing is
     * left untyped: a literal took the type of its place.
     */
    struct TypedNode {
        TypedNodeKind kind = TypedNodeKind::Constant;
        ExprType type;
        Opcode opcode = Opcode::Invalid;
        uint8_t operandCount = 0;
        Array<TypedNodeId, kMaxTypedOperands> operands{};
        RegisterImage constant{};
        SlotRef slot{};
        String path;
        /// `Operation`: the immediate of the instruction when the opcode has one (the index of a component for `Extract`)
        uint32_t immediate = 0;
        uint32_t column = 0;
    };

    /// A formula that passed the checker, in post-order: the root is the last node
    struct TypedExpr {
        Vector<TypedNode> nodes;
        TypedNodeId root = 0;
        ExprType type;

        [[nodiscard]] const TypedNode& operator[](TypedNodeId id) const {
            return nodes[id];
        }
    };

    /**
     * @brief Types a parsed formula against the declared context and the type its field has
     *
     * Every name path is linked with `LinkSlot` (so its messages, `did you mean` and the list of fields come from there) and must name a field of a program type or of a
     * reflected enum. There is no implicit conversion: two operands of two types are an error that says so, and the result must be exactly @p expected. An integer literal
     * takes the type of the operand beside it, or @p expected (int, int64, float or double); a float literal is float or double and an error where an integer is needed. A
     * bare name that is no level and no function is an enumerator when the type beside it (the other operand, the other value of `select`, @p expected) is an enum; a
     * `'text'` is a string. A component (`caster.velocity.x`, `lerp(a, b, t).y`) is a slot at the component's offset where the field stores it as the program computes with it, else
     * an `Extract`; two operands of two types are a mixed pair (`vec3 * float`) when `BE_PROGRAM_MIXED_ROWS` has it, and a literal beside a vector takes its scalar. A division or a
     * modulo by a literal zero, an operator or a function a type lacks, a component a type lacks, a wrong number of arguments and an @p expected outside the program types are
     * errors. An error is `{message, column of the node}`; the one about @p expected has column 0.
     */
    [[nodiscard]] std::expected<TypedExpr, ProgramError> CheckExpression(const ExprAst& ast, const ContextDecl& declaration, const TypeDesc& expected);

    /// The type a field of the type @p expected has in a formula (for an enum, with its descriptor), or the error that lists the types a formula can have: asked before the text is read, so a field no
    /// formula can fill says so first
    [[nodiscard]] std::expected<ExprType, ProgramError> CheckExpectedType(const TypeDesc& expected);

}  // namespace BECore
