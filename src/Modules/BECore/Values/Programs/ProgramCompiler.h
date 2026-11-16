#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueType.h>
#include <cstdint>
#include <cstring>
#include <expected>

namespace BECore {

    // The accessor of Value<T> includes this header, so the definition of `Value` is not visible here: `CompileValue` is a template and needs it only where it is used
    template <ValueType T>
    class Value;

    /// What a formula became: a constant, a read of one field, or a program of the pool
    CORE_ENUM(CompiledKind, uint8_t, Constant, Slot, Program)

    /**
     * @brief The result of `CompileExpression`
     *
     * `Constant` carries the register image of the result (the bytes of a value of `type`, zero-padded), `Slot` the field and the path as written, `Program` the id in
     * the `ProgramPool`. The other members of the other kinds are empty.
     */
    struct CompiledExpression {
        CompiledKind kind = CompiledKind::Constant;
        ProgramType type = ProgramType::Int32;
        RegisterImage constant{};
        SlotRef slot{};
        String path;
        ProgramId program = 0;
    };

    /**
     * @brief Text to a constant, a slot or a program: parse, check against @p declaration and @p expected, fold, lower
     *
     * A subtree of constants is replaced by the result of running it with the interpreter, so folding and running cannot disagree; nothing is simplified by algebra
     * (`a - a` runs). A tree stays a tree: equal subexpressions are computed twice, equal constants are one entry of the program's table. The registers are used
     * like a stack, post-order. A formula that is a constant or one field is not a program (`2 * 6` is the constant 12, `caster.attack` the slot), and a program is
     * interned by its code: the same code is one id however it was written, and a slot offset is part of the code, so the same text under two declarations is two programs.
     * Nothing is added to the pool when the text is refused. Allocates; it runs when content is loaded.
     */
    [[nodiscard]] std::expected<CompiledExpression, ProgramError> CompileExpression(StringView text, const ContextDecl& declaration, const TypeDesc& expected);

    /**
     * @brief The error as one line and a caret under the place: `<message> (column N)`, the text around it, `^`
     *
     * Long texts show a window around the column; a byte that is not printable ASCII is shown as `?`. An error with no column (0) is the message alone.
     */
    [[nodiscard]] String FormatProgramError(StringView text, const ProgramError& error);

    /// `CompileExpression` for a field of the C++ type @p T: the result as a `Value<T>` (`Const`, `Slot` or `Program`); a type no program computes is the error that lists the ones that can be
    template <ValueType T>
    [[nodiscard]] std::expected<Value<T>, ProgramError> CompileValue(StringView text, const ContextDecl& declaration) {
        const auto compiled = CompileExpression(text, declaration, DescribeType<T>());
        if (!compiled.has_value()) {
            return std::unexpected(compiled.error());
        }
        if constexpr (ProgramValue<T>) {
            switch (compiled->kind) {
                case CompiledKind::Constant: {
                    if constexpr (IsReflectedEnum<T>) {
                        ProgramEnum index;
                        std::memcpy(&index, compiled->constant.bytes.data(), sizeof(index));
                        return Value<T>::Const(static_cast<T>(index.index));
                    } else {
                        T literal{};
                        std::memcpy(&literal, compiled->constant.bytes.data(), sizeof(T));
                        return Value<T>::Const(literal);
                    }
                }
                case CompiledKind::Slot:
                    return Value<T>::FromSlot(compiled->slot, PoolString::Intern(compiled->path));
                case CompiledKind::Program:
                    return Value<T>::FromProgram(compiled->program, PoolString::Intern(text));
            }
        }
        ASSERT(false, "CompileValue: a program was made for a type no program computes");
        return std::unexpected(ProgramError{"no program computes this type", 0});
    }

}  // namespace BECore
