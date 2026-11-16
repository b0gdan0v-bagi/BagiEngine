#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// How a function reads its arguments: `SameType` takes arguments of one type and gives what the row gives (`length` of a vector is a float), `Select` a bool and two values of one
    /// type, `Convert` a number, `Pack` one argument per component of its type, `Blend` two values of one type and the weight (a float, or the type itself for a float or a double)
    CORE_ENUM(ExprFunctionShape, uint8_t, SameType, Select, Convert, Pack, Blend)

    /// A function a formula may call; whether a type has it is the row of `BE_PROGRAM_ROWS` (`OpcodeOf`), so a new row of a type needs no change here
    struct ExprFunction {
        StringView name;
        ExprFunctionShape shape = ExprFunctionShape::SameType;
        uint8_t arity = 0;
        /// `SameType` and `Select`: what the function does
        ProgramOperation operation = ProgramOperation::Abs;
        /// `Convert` and `Pack`: the type of the result
        ProgramType target = ProgramType::Int32;
    };

    inline constexpr size_t kExprFunctionCount = 23;

    /// The functions of the language; the conversions and the packs are named like the types a game writes (`int` is an `int32_t`). `lerp` of a `quat` is `Slerp`, the shortest arc
    /// between two rotations; `normalize` of a zero vector is zero and of a zero `quat` the identity (the engine's `Normalized`)
    inline constexpr Array<ExprFunction, kExprFunctionCount> kExprFunctions{{
        {"min", ExprFunctionShape::SameType, 2, ProgramOperation::Min, ProgramType::Int32},       {"max", ExprFunctionShape::SameType, 2, ProgramOperation::Max, ProgramType::Int32},
        {"clamp", ExprFunctionShape::SameType, 3, ProgramOperation::Clamp, ProgramType::Int32},   {"abs", ExprFunctionShape::SameType, 1, ProgramOperation::Abs, ProgramType::Int32},
        {"lerp", ExprFunctionShape::Blend, 3, ProgramOperation::Lerp, ProgramType::Int32},        {"floor", ExprFunctionShape::SameType, 1, ProgramOperation::Floor, ProgramType::Int32},
        {"ceil", ExprFunctionShape::SameType, 1, ProgramOperation::Ceil, ProgramType::Int32},     {"round", ExprFunctionShape::SameType, 1, ProgramOperation::Round, ProgramType::Int32},
        {"sqrt", ExprFunctionShape::SameType, 1, ProgramOperation::Sqrt, ProgramType::Int32},     {"dot", ExprFunctionShape::SameType, 2, ProgramOperation::Dot, ProgramType::Int32},
        {"length", ExprFunctionShape::SameType, 1, ProgramOperation::Length, ProgramType::Int32}, {"normalize", ExprFunctionShape::SameType, 1, ProgramOperation::Normalize, ProgramType::Int32},
        {"select", ExprFunctionShape::Select, 3, ProgramOperation::Select, ProgramType::Int32},   {"int", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Int32},
        {"int64", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Int64},      {"float", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Float},
        {"double", ExprFunctionShape::Convert, 1, ProgramOperation::Abs, ProgramType::Double},    {"vec2", ExprFunctionShape::Pack, 2, ProgramOperation::Pack, ProgramType::Vec2},
        {"vec3", ExprFunctionShape::Pack, 3, ProgramOperation::Pack, ProgramType::Vec3},          {"vec4", ExprFunctionShape::Pack, 4, ProgramOperation::Pack, ProgramType::Vec4},
        {"ivec2", ExprFunctionShape::Pack, 2, ProgramOperation::Pack, ProgramType::IVec2},        {"quat", ExprFunctionShape::Pack, 4, ProgramOperation::Pack, ProgramType::Quat},
        {"color", ExprFunctionShape::Pack, 4, ProgramOperation::Pack, ProgramType::Color},
    }};

    /// `at(i, e0, e1, ...)`, an inline table: it takes any number of arguments, so it has no row in `kExprFunctions` and the checker reads it on its own
    inline constexpr StringView kTableFunction = "at";

    /// `ByLevel('Name', i)`, an entry of a named table the game supplies (`IValueTables`): a string literal and an `int`, so it has no row either
    inline constexpr StringView kNamedTableFunction = "ByLevel";

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
            case ProgramType::Vec2:
                return "vec2";
            case ProgramType::Vec3:
                return "vec3";
            case ProgramType::Vec4:
                return "vec4";
            case ProgramType::IVec2:
                return "ivec2";
            case ProgramType::Quat:
                return "quat";
            case ProgramType::Color:
                return "color";
            case ProgramType::Enum:
                return "enum";
            case ProgramType::String:
                return "string";
        }
        return "?";
    }

    [[nodiscard]] constexpr bool IsFloatType(ProgramType type) {
        return type == ProgramType::Float || type == ProgramType::Double;
    }

    [[nodiscard]] constexpr bool IsNumberType(ProgramType type) {
        return type == ProgramType::Int32 || type == ProgramType::Int64 || IsFloatType(type);
    }

}  // namespace BECore
