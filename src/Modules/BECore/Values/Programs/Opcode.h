#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

// The one list of what a program can do. The opcode enum, the kernel table (`Kernels.h`), the interpreter's `switch`, the program printer and the matrix
// of `ProgramInterpreterTest` are all expanded from the lists below, so none of them can name an operation another lacks.

/// The types a program computes with: `X(name, C++ type)`. A new type is a row here, a value of `ProgramType` and rows in `BE_PROGRAM_ROWS`.
#define BE_PROGRAM_TYPES(X)                                                                                                                                                                            \
    X(Bool, bool)                                                                                                                                                                                      \
    X(Int32, int32_t)                                                                                                                                                                                  \
    X(Int64, int64_t)                                                                                                                                                                                  \
    X(Float, float)                                                                                                                                                                                    \
    X(Double, double)

// The types an operation is listed for: `X(operation, type name, C++ type)` per type. The kernels say whether the operation exists for a type
// (`Kernels.h`, by concepts); `kHasRow` and a `static_assert` there hold the list and the concepts together.
#define BE_PROGRAM_TYPES_BOOL(X, Op) X(Op, Bool, bool)
#define BE_PROGRAM_TYPES_INTEGERS(X, Op) X(Op, Int32, int32_t) X(Op, Int64, int64_t)
#define BE_PROGRAM_TYPES_FLOATS(X, Op) X(Op, Float, float) X(Op, Double, double)
#define BE_PROGRAM_TYPES_NUMBERS(X, Op) BE_PROGRAM_TYPES_INTEGERS(X, Op) BE_PROGRAM_TYPES_FLOATS(X, Op)
#define BE_PROGRAM_TYPES_ALL(X, Op) BE_PROGRAM_TYPES_BOOL(X, Op) BE_PROGRAM_TYPES_NUMBERS(X, Op)

/// Every typed instruction: `X(operation, type name, C++ type)`; the opcode is the two names written together (`AddInt32`)
#define BE_PROGRAM_ROWS(X)                                                                                                                                                                             \
    BE_PROGRAM_TYPES_ALL(X, LoadSlot)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_NUMBERS(X, Add)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_NUMBERS(X, Sub)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_NUMBERS(X, Mul)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_NUMBERS(X, Div)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_INTEGERS(X, Mod)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_NUMBERS(X, Neg)                                                                                                                                                                   \
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
    BE_PROGRAM_TYPES_FLOATS(X, Floor)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_FLOATS(X, Ceil)                                                                                                                                                                   \
    BE_PROGRAM_TYPES_FLOATS(X, Round)                                                                                                                                                                  \
    BE_PROGRAM_TYPES_FLOATS(X, Sqrt)

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

namespace BECore {

    /// What a program register holds; the order is the order of the `LoadSlot` rows (`LoadSlotOpcode`)
    CORE_ENUM(ProgramType, uint8_t, Bool, Int32, Int64, Float, Double)

    /**
     * @brief One instruction of a program
     *
     * `LoadConst` and `Copy` move a whole register whatever it holds; `LoadSlot<type>` reads a field of a context object; every other opcode is
     * an operation applied to one type (`AddInt32`), a conversion (`Int32ToFloat`). `Invalid` is never emitted: a zeroed instruction is caught.
     */
    CORE_ENUM(Opcode, uint16_t, Invalid, LoadConst, Copy BE_PROGRAM_ROWS(BE_PROGRAM_OPCODE_ROW) BE_PROGRAM_CONVERSIONS(BE_PROGRAM_OPCODE_CONVERSION))

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

    /// A C++ type a program can compute with
    template <typename T>
    concept ProgramScalar = requires {
        ProgramTypeTraits<T>::kType;
    };

    template <ProgramScalar T>
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

    static_assert(static_cast<size_t>(Opcode::LoadSlotDouble) - static_cast<size_t>(Opcode::LoadSlotBool) + 1 == EnumUtils<ProgramType>::Count(),
                  "the LoadSlot rows are one run, in the order of ProgramType");

    [[nodiscard]] constexpr bool IsLoadSlot(Opcode op) {
        return op >= Opcode::LoadSlotBool && op <= Opcode::LoadSlotDouble;
    }

}  // namespace BECore
