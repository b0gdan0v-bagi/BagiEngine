#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/CompileOptions.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/ValueType.h>
#include <algorithm>
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
     * the `ProgramPool`. The other members of the other kinds are empty. `domain` names the registered type of a result of type `Domain` and is `kNoDomainType` otherwise.
     */
    struct CompiledExpression {
        CompiledKind kind = CompiledKind::Constant;
        ProgramType type = ProgramType::Int32;
        DomainTypeId domain = kNoDomainType;
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
     * Nothing is added to the pool when the text is refused. Allocates; it runs when content is loaded. The tables @p options names are copied into the program: it keeps no pointer to the provider.
     */
    [[nodiscard]] std::expected<CompiledExpression, ProgramError> CompileExpression(StringView text, const ContextDecl& declaration, const TypeDesc& expected, const CompileOptions& options = {});

    /**
     * @brief The error as one line and a caret under the place: `<message> (column N)`, the text around it, `^`
     *
     * Long texts show a window around the column; a byte that is not printable ASCII is shown as `?`. An error with no column (0) is the message alone.
     */
    [[nodiscard]] String FormatProgramError(StringView text, const ProgramError& error);

    /**
     * @brief Compiles a formula text into the `Value<T>` a field of type @p T stores: the public entry point for code that holds a text and no document
     *
     * `CompileValue<bool>` is a condition, `CompileValue<int32_t>` (or any other `ValueType` a program computes) an argument. The declaration is always explicit: the paths of
     * @p text name levels of @p declaration, and the level numbers and field offsets are part of the program's code, so the same text under two declarations is two programs. The result is
     * the very `Value` the accessor of a document stores for `expr`: a `Const` when the text folds to a constant, a `Slot` when it is one field (`Kind()` says which), else a
     * `Program` of the `ProgramPool` and its text; evaluate any of them with `Evaluate(context)`. @p options carries the tables of `ByLevel` and `fuse`; the entries of a table
     * are copied, nothing is kept of the provider.
     *
     * It allocates (parse, check, lower) and may add one program to the pool: call it when content loads, never per frame. Thread-safe: the pool is, and nothing else is shared.
     * A refused text adds nothing and gives a `ProgramError` whose column is a byte of @p text (`FormatProgramError` shows it). @p T is a type a program computes or a type a
     * game registered (`ProgramDomain`); any other (`Rect`, an unregistered struct) is the error that lists the types that can be.
     */
    template <ValueType T>
    [[nodiscard]] std::expected<Value<T>, ProgramError> CompileValue(StringView text, const ContextDecl& declaration, const CompileOptions& options = {}) {
        const auto compiled = CompileExpression(text, declaration, DescribeType<T>(), options);
        if (!compiled.has_value()) {
            return std::unexpected(compiled.error());
        }
        // A type the checker accepted is a program type or a registered one (`CheckExpectedType`), so the same three outcomes hold for both
        switch (compiled->kind) {
            case CompiledKind::Constant: {
                if constexpr (IsReflectedEnum<T>) {
                    ProgramEnum index;
                    std::memcpy(&index, compiled->constant.bytes.data(), sizeof(index));
                    return Value<T>::Const(static_cast<T>(index.index));
                } else {
                    T literal{};
                    std::memcpy(&literal, compiled->constant.bytes.data(), std::min(sizeof(T), compiled->constant.bytes.size()));
                    return Value<T>::Const(literal);
                }
            }
            case CompiledKind::Slot:
                return Value<T>::FromSlot(compiled->slot, PoolString::Intern(compiled->path));
            case CompiledKind::Program:
                return Value<T>::FromProgram(compiled->program, PoolString::Intern(text));
        }
        ASSERT(false, "CompileValue: the compiler made a kind that does not exist");
        return std::unexpected(ProgramError{"the compiler made a kind that does not exist", 0});
    }

}  // namespace BECore
