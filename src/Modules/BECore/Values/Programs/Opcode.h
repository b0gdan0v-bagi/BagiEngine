#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

// The one list of what a program can do. The opcode enum, the kernel table (`Kernels.h`), the interpreter's `switch`, the program printer and the matrix
// of `ProgramInterpreterTest` are all expanded from the lists below, so none of them can name an operation another lacks.

/// The types a program computes with: `X(name, C++ type)`, in the order of `ProgramType`. A new type is a row here, a value of `ProgramType` and rows in `BE_PROGRAM_ROWS`.
/// `ProgramEnum` stands for every reflected enum and `PoolString` is the text.
#define BE_PROGRAM_TYPES(X)                                                                                                                                                                            \
    X(Bool, bool)                                                                                                                                                                                      \
    X(Int32, int32_t)                                                                                                                                                                                  \
    X(Int64, int64_t)                                                                                                                                                                                  \
    X(Float, float)                                                                                                                                                                                    \
    X(Double, double)                                                                                                                                                                                  \
    X(Vec2, Vec2)                                                                                                                                                                                      \
    X(Vec3, Vec3)                                                                                                                                                                                      \
    X(Vec4, Vec4)                                                                                                                                                                                      \
    X(IVec2, IVec2)                                                                                                                                                                                    \
    X(Quat, Quat)                                                                                                                                                                                      \
    X(Color, Color)                                                                                                                                                                                    \
    X(Enum, ProgramEnum)                                                                                                                                                                               \
    X(String, PoolString)

// The types an operation is listed for: `X(operation, type name, C++ type)` per type. The kernels say whether the operation exists for a type
// (`Kernels.h`, by concepts); `kHasRow` and a `static_assert` there hold the list and the concepts together.
#define BE_PROGRAM_TYPES_BOOL(X, Op) X(Op, Bool, bool)
#define BE_PROGRAM_TYPES_INTEGERS(X, Op) X(Op, Int32, int32_t) X(Op, Int64, int64_t)
#define BE_PROGRAM_TYPES_FLOATS(X, Op) X(Op, Float, float) X(Op, Double, double)
#define BE_PROGRAM_TYPES_NUMBERS(X, Op) BE_PROGRAM_TYPES_INTEGERS(X, Op) BE_PROGRAM_TYPES_FLOATS(X, Op)
#define BE_PROGRAM_TYPES_SCALARS(X, Op) BE_PROGRAM_TYPES_BOOL(X, Op) BE_PROGRAM_TYPES_NUMBERS(X, Op)
#define BE_PROGRAM_TYPES_FLOAT_VECTORS(X, Op) X(Op, Vec2, Vec2) X(Op, Vec3, Vec3) X(Op, Vec4, Vec4)
#define BE_PROGRAM_TYPES_MATH(X, Op) X(Op, Vec2, Vec2) X(Op, Vec3, Vec3) X(Op, Vec4, Vec4) X(Op, IVec2, IVec2) X(Op, Quat, Quat) X(Op, Color, Color)
#define BE_PROGRAM_TYPES_ENUM(X, Op) X(Op, Enum, ProgramEnum)
#define BE_PROGRAM_TYPES_STRING(X, Op) X(Op, String, PoolString)
#define BE_PROGRAM_TYPES_ALL(X, Op) BE_PROGRAM_TYPES_SCALARS(X, Op) BE_PROGRAM_TYPES_MATH(X, Op) BE_PROGRAM_TYPES_ENUM(X, Op) BE_PROGRAM_TYPES_STRING(X, Op)

/// Every typed instruction: `X(operation, type name, C++ type)`; the opcode is the two names written together (`AddInt32`)
#define BE_PROGRAM_ROWS(X)                                                                                                                                                                             \
    BE_PROGRAM_TYPES_ALL(X, LoadSlot)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_ALL(X, LoadElem)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_NUMBERS(X, Add)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_FLOAT_VECTORS(X, Add)                                                                                                                                                             \
    X(Add, IVec2, IVec2)                                                                                                                                                                               \
    BE_PROGRAM_TYPES_NUMBERS(X, Sub)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_FLOAT_VECTORS(X, Sub)                                                                                                                                                             \
    X(Sub, IVec2, IVec2)                                                                                                                                                                               \
    BE_PROGRAM_TYPES_NUMBERS(X, Mul)                                                                                                                                                                   \
    X(Mul, Quat, Quat)                                                                                                                                                                                 \
    BE_PROGRAM_TYPES_NUMBERS(X, Div)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_INTEGERS(X, Mod)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_NUMBERS(X, Neg)                                                                                                                                                                   \
    X(Neg, Vec3, Vec3)                                                                                                                                                                                 \
    X(Neg, Vec4, Vec4)                                                                                                                                                                                 \
    X(Neg, IVec2, IVec2)                                                                                                                                                                               \
    BE_PROGRAM_TYPES_NUMBERS(X, Abs)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_ALL(X, Eq)                                                                                                                                                                        \
    BE_PROGRAM_TYPES_ALL(X, Ne)                                                                                                                                                                        \
    BE_PROGRAM_TYPES_NUMBERS(X, Lt)                                                                                                                                                                    \
    BE_PROGRAM_TYPES_NUMBERS(X, Le)                                                                                                                                                                    \
    BE_PROGRAM_TYPES_NUMBERS(X, Gt)                                                                                                                                                                    \
    BE_PROGRAM_TYPES_NUMBERS(X, Ge)                                                                                                                                                                    \
    BE_PROGRAM_TYPES_BOOL(X, And)                                                                                                                                                                      \
    BE_PROGRAM_TYPES_BOOL(X, Or)                                                                                                                                                                       \
    BE_PROGRAM_TYPES_BOOL(X, Not)                                                                                                                                                                      \
    BE_PROGRAM_TYPES_NUMBERS(X, Min)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_NUMBERS(X, Max)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_NUMBERS(X, Clamp)                                                                                                                                                                 \
    BE_PROGRAM_TYPES_ALL(X, Select)                                                                                                                                                                    \
    BE_PROGRAM_TYPES_FLOATS(X, Lerp)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_FLOAT_VECTORS(X, Lerp)                                                                                                                                                            \
    X(Lerp, Color, Color)                                                                                                                                                                              \
    X(Lerp, Quat, Quat)                                                                                                                                                                                \
    BE_PROGRAM_TYPES_FLOATS(X, Floor)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_FLOATS(X, Ceil)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_FLOATS(X, Round)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_FLOATS(X, Sqrt)                                                                                                                                                                   \
    X(Dot, Vec3, Vec3)                                                                                                                                                                                 \
    X(Dot, Vec4, Vec4)                                                                                                                                                                                 \
    X(Dot, Quat, Quat)                                                                                                                                                                                 \
    X(Length, Vec3, Vec3)                                                                                                                                                                              \
    X(Length, Vec4, Vec4)                                                                                                                                                                              \
    X(Normalize, Vec3, Vec3)                                                                                                                                                                           \
    X(Normalize, Vec4, Vec4)                                                                                                                                                                           \
    X(Normalize, Quat, Quat)                                                                                                                                                                           \
    BE_PROGRAM_TYPES_MATH(X, Extract)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_MATH(X, Pack)

/// The typed instructions the peephole pass (`Peephole.h`) writes: `X(operation, type name, C++ type)`; `LoadTableSlot`, which holds any type, is an opcode of its own. Each
/// computes what the instructions it replaces compute, bit for bit; none is in `BE_PROGRAM_ROWS`, which are the operations of the language
#define BE_PROGRAM_FUSED_ROWS(X) BE_PROGRAM_TYPES_NUMBERS(X, MulAddSlot) X(MulAddSlot, Vec3, Vec3) BE_PROGRAM_TYPES_FLOATS(X, LerpSlots)

/// The operations whose two operands have different types: `X(operation, lhs name, lhs C++ type, rhs name, rhs C++ type)`; the opcode is `MulVec3Float`.
/// A vector by its scalar (`float` for `Vec2` .. `Vec4`, `int` for `IVec2`) and `Quat * Vec3` (a rotation), each where the C++ operator or function exists
/// (`Kernels.h` holds the list and the concepts together). Two numbers never mix: that is the explicit conversion of the language.
#define BE_PROGRAM_MIXED_ROWS(X)                                                                                                                                                                       \
    X(Mul, Vec2, Vec2, Float, float)                                                                                                                                                                   \
    X(Mul, Vec3, Vec3, Float, float)                                                                                                                                                                   \
    X(Mul, Vec4, Vec4, Float, float)                                                                                                                                                                   \
    X(Mul, Float, float, Vec3, Vec3)                                                                                                                                                                   \
    X(Mul, Float, float, Vec4, Vec4)                                                                                                                                                                   \
    X(Mul, IVec2, IVec2, Int32, int32_t)                                                                                                                                                               \
    X(Mul, Int32, int32_t, IVec2, IVec2)                                                                                                                                                               \
    X(Mul, Quat, Quat, Vec3, Vec3)                                                                                                                                                                     \
    X(Div, Vec3, Vec3, Float, float)                                                                                                                                                                   \
    X(Div, Vec4, Vec4, Float, float)

/// The conversions between the four numbers: `X(from name, from C++ type, to name, to C++ type)`; the opcode is `Int32ToFloat`
#define BE_PROGRAM_CONVERSIONS(X)                                                                                                                                                                      \
    X(Int32, int32_t, Int64, int64_t)                                                                                                                                                                  \
    X(Int32, int32_t, Float, float)                                                                                                                                                                    \
    X(Int32, int32_t, Double, double)                                                                                                                                                                  \
    X(Int64, int64_t, Int32, int32_t)                                                                                                                                                                  \
    X(Int64, int64_t, Float, float)                                                                                                                                                                    \
    X(Int64, int64_t, Double, double)                                                                                                                                                                  \
    X(Float, float, Int32, int32_t)                                                                                                                                                                    \
    X(Float, float, Int64, int64_t)                                                                                                                                                                    \
    X(Float, float, Double, double)                                                                                                                                                                    \
    X(Double, double, Int32, int32_t)                                                                                                                                                                  \
    X(Double, double, Int64, int64_t)                                                                                                                                                                  \
    X(Double, double, Float, float)

#define BE_PROGRAM_OPCODE_ROW(Op, Type, Cpp) , Op##Type
#define BE_PROGRAM_OPCODE_CONVERSION(Src, SrcCpp, Dst, DstCpp) , Src##To##Dst
#define BE_PROGRAM_OPCODE_MIXED(Op, LName, LCpp, RName, RCpp) , Op##LName##RName

namespace BECore {

    /**
     * @brief The register image of an enum: the index of the enumerator
     *
     * The values of a `CORE_ENUM` are 0..N-1 in declaration order, so the index is the value and the names are `TypeDesc::names`. Which enum it is
     * is not in the image or the opcode: the checker keeps the descriptor beside the type, so two enums never mix.
     */
    struct ProgramEnum {
        uint32_t index = 0;

        [[nodiscard]] constexpr bool operator==(const ProgramEnum&) const = default;
    };

    /// What a program register holds; the order is the order of the `LoadSlot` rows (`LoadSlotOpcode`)
    CORE_ENUM(ProgramType, uint8_t, Bool, Int32, Int64, Float, Double, Vec2, Vec3, Vec4, IVec2, Quat, Color, Enum, String)

    /**
     * @brief One instruction of a program
     *
     * `LoadConst`, `Copy`, `LoadTable` and `LoadTableSlot` move a whole register whatever it holds (a table picks it from a run of constants by an index);
     * `LoadSlot<type>` reads a field of a context object; `ListSlot`, `ListElem`, `ListSize`, `ListAt`, `LoopBegin`, `LoopEnd` and `BreakIf` read lists and loop over them (`Lists/`),
     * `LoadElem<type>` reads a field of an element; every other opcode is an operation applied to one type (`AddInt32`), a conversion
     * (`Int32ToFloat`) or a fused instruction (`BE_PROGRAM_FUSED_ROWS`; `Instruction.h` says how each is encoded). `Invalid` is never emitted: a zeroed instruction is caught.
     */
    CORE_ENUM(Opcode, uint16_t, Invalid, LoadConst, Copy, LoadTable, LoadTableSlot, ListSlot, ListElem, ListSize, ListAt, LoopBegin, LoopEnd,
              BreakIf BE_PROGRAM_ROWS(BE_PROGRAM_OPCODE_ROW) BE_PROGRAM_FUSED_ROWS(BE_PROGRAM_OPCODE_ROW) BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_OPCODE_MIXED)
                  BE_PROGRAM_CONVERSIONS(BE_PROGRAM_OPCODE_CONVERSION))

    /// The operations of `BE_PROGRAM_ROWS` by name, apart from the type: what the text of a formula says (`+`, `min`) before a type is known
    CORE_ENUM(ProgramOperation, uint8_t, LoadSlot, Add, Sub, Mul, Div, Mod, Neg, Abs, Eq, Ne, Lt, Le, Gt, Ge, And, Or, Not, Min, Max, Clamp, Select, Lerp, Floor, Ceil, Round, Sqrt, Dot, Length,
              Normalize, Extract, Pack, LoadElem)

    /// The opcode of an operation on a type, `Invalid` when the type has no row: the checker asks it instead of keeping a table of its own
    [[nodiscard]] constexpr Opcode OpcodeOf(ProgramOperation operation, ProgramType type) {
#define BE_PROGRAM_OPCODE_OF(Op, Type, Cpp)                                                                                                                                                            \
    if (operation == ProgramOperation::Op && type == ProgramType::Type) {                                                                                                                              \
        return Opcode::Op##Type;                                                                                                                                                                       \
    }
        BE_PROGRAM_ROWS(BE_PROGRAM_OPCODE_OF)
#undef BE_PROGRAM_OPCODE_OF
        return Opcode::Invalid;
    }

    /// The opcode of an operation on two operands of different types, `Invalid` when the pair has no row (`BE_PROGRAM_MIXED_ROWS`)
    [[nodiscard]] constexpr Opcode MixedOpcodeOf(ProgramOperation operation, ProgramType lhs, ProgramType rhs) {
#define BE_PROGRAM_MIXED_OPCODE_OF(Op, LName, LCpp, RName, RCpp)                                                                                                                                       \
    if (operation == ProgramOperation::Op && lhs == ProgramType::LName && rhs == ProgramType::RName) {                                                                                                 \
        return Opcode::Op##LName##RName;                                                                                                                                                               \
    }
        BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_MIXED_OPCODE_OF)
#undef BE_PROGRAM_MIXED_OPCODE_OF
        return Opcode::Invalid;
    }

    /// The conversion between two numbers, `Invalid` for the same type or a type that is no number
    [[nodiscard]] constexpr Opcode ConversionOpcode(ProgramType from, ProgramType to) {
#define BE_PROGRAM_CONVERSION_OF(Src, SrcCpp, Dst, DstCpp)                                                                                                                                             \
    if (from == ProgramType::Src && to == ProgramType::Dst) {                                                                                                                                          \
        return Opcode::Src##To##Dst;                                                                                                                                                                   \
    }
        BE_PROGRAM_CONVERSIONS(BE_PROGRAM_CONVERSION_OF)
#undef BE_PROGRAM_CONVERSION_OF
        return Opcode::Invalid;
    }

    /// The program type of a C++ type, specialized for each row of `BE_PROGRAM_TYPES`; a type without a specialization is no program type
    template <typename T>
    struct ProgramTypeTraits;

#define BE_PROGRAM_TYPE_TRAITS(Name, Cpp)                                                                                                                                                              \
    template <>                                                                                                                                                                                        \
    struct ProgramTypeTraits<Cpp> {                                                                                                                                                                    \
        static constexpr ProgramType kType = ProgramType::Name;                                                                                                                                        \
    };
    BE_PROGRAM_TYPES(BE_PROGRAM_TYPE_TRAITS)
#undef BE_PROGRAM_TYPE_TRAITS

    /// Every reflected enum is the program type `Enum`; the register holds its index (`Run<E>` and `ProgramBuilder` cast)
    template <IsReflectedEnum T>
    struct ProgramTypeTraits<T> {
        static constexpr ProgramType kType = ProgramType::Enum;
    };

    /// A C++ type a program can compute with: a row of `BE_PROGRAM_TYPES` or a reflected enum
    template <typename T>
    concept ProgramValue = requires {
        ProgramTypeTraits<T>::kType;
    };

    template <ProgramValue T>
    inline constexpr ProgramType kProgramTypeOf = ProgramTypeTraits<T>::kType;

    /// The size in bytes of a value of the type, what `Run` writes to its result
    [[nodiscard]] constexpr size_t ProgramTypeSize(ProgramType type) {
        switch (type) {
#define BE_PROGRAM_TYPE_SIZE(Name, Cpp)                                                                                                                                                                \
    case ProgramType::Name:                                                                                                                                                                            \
        return sizeof(Cpp);
            BE_PROGRAM_TYPES(BE_PROGRAM_TYPE_SIZE)
#undef BE_PROGRAM_TYPE_SIZE
        }
        return 0;
    }

#define BE_PROGRAM_TYPE_COUNT(Name, Cpp) +1
    static_assert(EnumUtils<ProgramType>::Count() == (0 BE_PROGRAM_TYPES(BE_PROGRAM_TYPE_COUNT)), "BE_PROGRAM_TYPES and ProgramType list the same types");
#undef BE_PROGRAM_TYPE_COUNT

    /// The opcode that reads a field of this type from a context object
    [[nodiscard]] constexpr Opcode LoadSlotOpcode(ProgramType type) {
        switch (type) {
#define BE_PROGRAM_LOAD_SLOT(Name, Cpp)                                                                                                                                                                \
    case ProgramType::Name:                                                                                                                                                                            \
        return Opcode::LoadSlot##Name;
            BE_PROGRAM_TYPES(BE_PROGRAM_LOAD_SLOT)
#undef BE_PROGRAM_LOAD_SLOT
        }
        return Opcode::Invalid;
    }

    static_assert(static_cast<size_t>(Opcode::LoadSlotString) - static_cast<size_t>(Opcode::LoadSlotBool) + 1 == EnumUtils<ProgramType>::Count(),
                  "the LoadSlot rows are one run, in the order of ProgramType");

    [[nodiscard]] constexpr bool LoadSlotRowsFollowTheTypes() {
        for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
            if (static_cast<size_t>(LoadSlotOpcode(type)) - static_cast<size_t>(Opcode::LoadSlotBool) != static_cast<size_t>(type)) {
                return false;
            }
        }
        return true;
    }

    static_assert(LoadSlotRowsFollowTheTypes(), "LoadSlotOpcode(type) is the row at the position of the type");

    [[nodiscard]] constexpr bool IsLoadSlot(Opcode op) {
        return op >= Opcode::LoadSlotBool && op <= Opcode::LoadSlotString;
    }

    static_assert(static_cast<size_t>(Opcode::LoadElemString) - static_cast<size_t>(Opcode::LoadElemBool) + 1 == EnumUtils<ProgramType>::Count(),
                  "the LoadElem rows are one run, in the order of ProgramType");

    /// The opcode that reads a field of this type from an element of a list
    [[nodiscard]] constexpr Opcode LoadElemOpcode(ProgramType type) {
        switch (type) {
#define BE_PROGRAM_LOAD_ELEM(Name, Cpp)                                                                                                                                                                \
    case ProgramType::Name:                                                                                                                                                                            \
        return Opcode::LoadElem##Name;
            BE_PROGRAM_TYPES(BE_PROGRAM_LOAD_ELEM)
#undef BE_PROGRAM_LOAD_ELEM
        }
        return Opcode::Invalid;
    }

    [[nodiscard]] constexpr bool IsLoadElem(Opcode op) {
        return op >= Opcode::LoadElemBool && op <= Opcode::LoadElemString;
    }

    /// The instructions that leave straight-line order: the interpreter and the generated code branch on what the kernel returns, and `Finish` accepts them only in the shapes of `Lists/`
    [[nodiscard]] constexpr bool IsJump(Opcode op) {
        return op >= Opcode::LoopBegin && op <= Opcode::BreakIf;
    }

    /// A list or loop instruction (the `LoadElem` rows are plain reads and not among them): the code of a program that has one is not straight-line, so the peephole pass leaves it alone
    [[nodiscard]] constexpr bool IsListInstruction(Opcode op) {
        return op >= Opcode::ListSlot && op <= Opcode::BreakIf;
    }

    /// `slot * k + b` over a number, or over a `Vec3` with a `float` factor: one load, one multiplication and one addition in one instruction
    [[nodiscard]] constexpr bool IsMulAddSlot(Opcode op) {
        switch (op) {
#define BE_PROGRAM_IS_MULADD(Op, Type, Cpp)                                                                                                                                                            \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return true;
            BE_PROGRAM_TYPES_NUMBERS(BE_PROGRAM_IS_MULADD, MulAddSlot)
            BE_PROGRAM_IS_MULADD(MulAddSlot, Vec3, Vec3)
#undef BE_PROGRAM_IS_MULADD
            default:
                break;
        }
        return false;
    }

    /// `lerp(slot, slot, t)` over a float
    [[nodiscard]] constexpr bool IsLerpSlots(Opcode op) {
        switch (op) {
#define BE_PROGRAM_IS_LERPSLOTS(Op, Type, Cpp)                                                                                                                                                         \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return true;
            BE_PROGRAM_TYPES_FLOATS(BE_PROGRAM_IS_LERPSLOTS, LerpSlots)
#undef BE_PROGRAM_IS_LERPSLOTS
            default:
                break;
        }
        return false;
    }

}  // namespace BECore
