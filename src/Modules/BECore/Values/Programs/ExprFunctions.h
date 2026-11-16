#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// How a function reads its arguments: `SameType` takes arguments of one type and gives that type, `Select` a bool and two values of one type, `Convert` any number
    CORE_ENUM(ExprFunctionShape, uint8_t, SameType, Select, Convert)

    /// A function a formula may call; whether a type has it is the row of `BE_PROGRAM_ROWS` (`OpcodeOf`), so a new row of a type needs no change here
    struct ExprFunction {
        StringView name;
        ExprFunctionShape shape = ExprFunctionShape::SameType;
        uint8_t arity = 0;
        /// `SameType` and `Select`: what the function does
        ProgramOperation operation = ProgramOperation::Abs;
        /// `Convert`: the type of the result
        ProgramType target = ProgramType::Int32;
    };

    inline constexpr size_t kExprFunctionCount = 14;

    /// The functions of the language; the four conversions are named like the types a game writes (`int` is an `int32_t`)
    inline constexpr Array<ExprFunction, kExprFunctionCount> kExprFunctions{{
        {"min", ExprFunctionShape::SameType, 2, ProgramOperation::Min, ProgramType::Int32},
        {"max", ExprFunctionShape::SameType, 2, ProgramOperation::Max, ProgramType::Int32},
        {"clamp", ExprFunctionShape::SameType, 3, ProgramOperation::Clamp, ProgramType::Int32},
        {"abs", ExprFunctionShape::SameType, 1, ProgramOperation::Abs, ProgramType::Int32},
        {"lerp", ExprFunctionShape::SameType, 3, ProgramOperation::Lerp, ProgramType::Int32},
        {"floor", ExprFunctionShape::SameType, 1, ProgramOperation::Floor, ProgramType::Int32},
        {"ceil", ExprFunctionShape::SameType, 1, ProgramOperation::Ceil, ProgramType::Int32},
        {"round", ExprFunctionShape::SameType, 1, ProgramOperation::Round, ProgramType::Int32},
        {"sqrt", ExprFunctionShape::SameType, 1, ProgramOperation::Sqrt, ProgramType::Int32},
        {"select", ExprFunctionShape::Select, 3, ProgramOperation::Select, ProgramType::Int32},
        {"int", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Int32},
        {"int64", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Int64},
        {"float", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Float},
        {"double", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Double},
    }};

    /// The function of this name, null when there is none
    [[nodiscard]] constexpr const ExprFunction* FindExprFunction(StringView name) {
        for (const ExprFunction& function : kExprFunctions) {
            if (function.name == name) {
                return &function;
            }
        }
        return nullptr;
    }

    /// The type as a formula and its messages name it: what a game writes in a conversion (`int` for `int32_t`)
    [[nodiscard]] constexpr StringView ProgramTypeName(ProgramType type) {
        switch (type) {
            case ProgramType::Bool:
                return "bool";
            case ProgramType::Int32:
                return "int";
            case ProgramType::Int64:
                return "int64";
            case ProgramType::Float:
                return "float";
            case ProgramType::Double:
                return "double";
        }
        return "?";
    }

    [[nodiscard]] constexpr bool IsNumberType(ProgramType type) {
        return type != ProgramType::Bool;
    }

    [[nodiscard]] constexpr bool IsFloatType(ProgramType type) {
        return type == ProgramType::Float || type == ProgramType::Double;
    }

}  // namespace BECore
