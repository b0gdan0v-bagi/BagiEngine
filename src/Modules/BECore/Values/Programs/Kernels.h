#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Assert/AssertMacros.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

/**
 * The meaning of every operation of a program, one struct per operation, over every type that has it. Each is total: no operation has undefined
 * behaviour or an exception, so a folded constant and a run agree (the rules are listed in `Programs/CLAUDE.md`). An operation has a type when its C++
 * operator or function does (`kSupports`, a concept), and `BE_PROGRAM_ROWS` lists exactly those: the `static_assert` at the end of the file holds
 * the two together.
 */
namespace BECore::Kernel {

    /// How an operation reads its registers and what it writes
    CORE_ENUM(OpShape, uint8_t, Slot, Unary, Binary, Compare, Ternary, Blend, Select)

    template <typename T>
    concept Integer = std::is_integral_v<T> && !std::is_same_v<T, bool>;

    template <typename T>
    concept Number = Integer<T> || std::floating_point<T>;

    template <typename T>
    [[nodiscard]] inline T Load(const RegisterImage& reg) {
        T value{};
        std::memcpy(&value, reg.bytes.data(), sizeof(T));
        return value;
    }

    template <typename T>
    inline void Store(RegisterImage& reg, T value) {
        std::memcpy(reg.bytes.data(), &value, sizeof(T));
    }

    /// `0 - a` in two's complement: the minimum stays the minimum
    template <Integer T>
    [[nodiscard]] constexpr T WrapNegate(T a) {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<U>(U{0} - static_cast<U>(a)));
    }

    template <Integer T>
    [[nodiscard]] constexpr T WrapAdd(T a, T b) {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<U>(static_cast<U>(a) + static_cast<U>(b)));
    }

    template <Integer T>
    [[nodiscard]] constexpr T WrapSub(T a, T b) {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<U>(static_cast<U>(a) - static_cast<U>(b)));
    }

    template <Integer T>
    [[nodiscard]] constexpr T WrapMul(T a, T b) {
        using U = std::make_unsigned_t<T>;
        return static_cast<T>(static_cast<U>(static_cast<U>(a) * static_cast<U>(b)));
    }

    /// The scalar a vector is multiplied by: `float` for the float vectors, `int` for `IVec2`, `void` for every other type
    template <typename V>
    struct VectorScalarTraits {
        using Type = void;
    };

    template <>
    struct VectorScalarTraits<Vec2> {
        using Type = float;
    };

    template <>
    struct VectorScalarTraits<Vec3> {
        using Type = float;
    };

    template <>
    struct VectorScalarTraits<Vec4> {
        using Type = float;
    };

    template <>
    struct VectorScalarTraits<IVec2> {
        using Type = int32_t;
    };

    /// A vector and exactly its scalar, in either order: the C++ operators also take any other number (`Vec3 * int`), which a program refuses (no implicit conversion)
    template <typename L, typename R>
    concept VectorAndScalar = (!std::is_void_v<typename VectorScalarTraits<L>::Type> && std::is_same_v<R, typename VectorScalarTraits<L>::Type>) ||
                              (!std::is_void_v<typename VectorScalarTraits<R>::Type> && std::is_same_v<L, typename VectorScalarTraits<R>::Type>);

    /// The float to integer rule: toward zero, saturating at the range of @p To, 0 for NaN
    template <Integer To, std::floating_point From>
    [[nodiscard]] constexpr To FloatToInteger(From value) {
        if (value != value) {
            return To{0};
        }
        // The bound rounds up to a power of two when @p To has more bits than the mantissa: every value at or above it is out of range, every one below it is not
        constexpr From kUpper = static_cast<From>(std::numeric_limits<To>::max());
        constexpr From kLower = static_cast<From>(std::numeric_limits<To>::lowest());
        if (value >= kUpper) {
            return std::numeric_limits<To>::max();
        }
        if (value <= kLower) {
            return std::numeric_limits<To>::lowest();
        }
        return static_cast<To>(value);
    }

    /// `double` to `float`, rounding to nearest: beyond the largest `float` and half a step more it is infinity (the plain cast is undefined there)
    [[nodiscard]] constexpr float DoubleToFloat(double value) {
        constexpr double kRoundsToInfinity = 0x1.ffffffp127;
        if (value >= kRoundsToInfinity) {
            return std::numeric_limits<float>::infinity();
        }
        if (value <= -kRoundsToInfinity) {
            return -std::numeric_limits<float>::infinity();
        }
        return static_cast<float>(value);
    }

    template <typename From, typename To>
    [[nodiscard]] constexpr To Convert(From value) {
        if constexpr (std::floating_point<From> && Integer<To>) {
            return FloatToInteger<To>(value);
        } else if constexpr (std::is_same_v<From, double> && std::is_same_v<To, float>) {
            return DoubleToFloat(value);
        } else {
            return static_cast<To>(value);
        }
    }

    /// The index of an enumerator from the @p bytes of the enum's field (1, 2, 4 or 8), zero-extended; a stored value beyond 32 bits keeps its low bits (the interpreter never traps)
    [[nodiscard]] inline uint32_t LoadEnumIndex(const uint8_t* field, uint8_t bytes) {
        switch (bytes) {
            case sizeof(uint8_t):
                return field[0];
            case sizeof(uint16_t): {
                uint16_t value = 0;
                std::memcpy(&value, field, sizeof(value));
                return value;
            }
            case sizeof(uint32_t): {
                uint32_t value = 0;
                std::memcpy(&value, field, sizeof(value));
                return value;
            }
            default: {
                ASSERT(bytes == sizeof(uint64_t), "Program: an enum field is 1, 2, 4 or 8 bytes");
                uint64_t value = 0;
                std::memcpy(&value, field, sizeof(value));
                return static_cast<uint32_t>(value);
            }
        }
    }

    /// `Color` has no `operator==`: it is equal when its four channels are
    template <typename T>
    concept EqualityComparable = std::is_same_v<T, Color> || requires(T a, T b) {
        {a == b}->std::convertible_to<bool>;
    };

    template <EqualityComparable T>
    [[nodiscard]] constexpr bool Equal(const T& a, const T& b) {
        if constexpr (std::is_same_v<T, Color>) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        } else {
            return a == b;
        }
    }

    struct LoadSlot {
        static constexpr OpShape kShape = OpShape::Slot;
        template <typename T>
        static constexpr bool kSupports = true;
    };

    struct Add {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = requires(T a, T b) {
            {a + b}->std::same_as<T>;
        };

        /// `IVec2` wraps like the integers: the engine's `operator+` overflows signed, so the kernel adds through unsigned words
        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (Integer<T>) {
                return WrapAdd(a, b);
            } else if constexpr (std::is_same_v<T, IVec2>) {
                return IVec2{WrapAdd(a.x, b.x), WrapAdd(a.y, b.y)};
            } else {
                return a + b;
            }
        }
    };

    struct Sub {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = requires(T a, T b) {
            {a - b}->std::same_as<T>;
        };

        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (Integer<T>) {
                return WrapSub(a, b);
            } else if constexpr (std::is_same_v<T, IVec2>) {
                return IVec2{WrapSub(a.x, b.x), WrapSub(a.y, b.y)};
            } else {
                return a - b;
            }
        }
    };

    struct Mul {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = requires(T a, T b) {
            {a * b}->std::same_as<T>;
        };

        /// A vector with its scalar, or a rotation with the vector it turns (`Quat::Rotate`)
        template <typename L, typename R>
        static constexpr bool kSupportsMixed = (VectorAndScalar<L, R> && requires(L a, R b) { a* b; }) || (std::is_same_v<L, Quat> && std::is_same_v<R, Vec3> && requires(L a, R b) { a.Rotate(b); });

        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (Integer<T>) {
                return WrapMul(a, b);
            } else {
                return a * b;
            }
        }

        template <typename L, typename R>
        [[nodiscard]] static auto ApplyMixed(L a, R b) {
            if constexpr (std::is_same_v<L, Quat>) {
                return a.Rotate(b);
            } else if constexpr (std::is_same_v<L, IVec2>) {
                return IVec2{WrapMul(a.x, b), WrapMul(a.y, b)};
            } else if constexpr (std::is_same_v<R, IVec2>) {
                return IVec2{WrapMul(a, b.x), WrapMul(a, b.y)};
            } else {
                return a * b;
            }
        }
    };

    struct Div {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = requires(T a, T b) {
            {a / b}->std::same_as<T>;
        };

        template <typename L, typename R>
        static constexpr bool kSupportsMixed = VectorAndScalar<L, R> && requires(L a, R b) {
            a / b;
        };

        /// Integers: a zero divisor gives 0 and `-1` negates with wrap-around, so the minimum divided by -1 is the minimum. Floats: IEEE
        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (Integer<T>) {
                if (b == T{0}) {
                    return T{0};
                }
                if (b == T{-1}) {
                    return WrapNegate(a);
                }
            }
            return a / b;
        }

        template <typename L, typename R>
        [[nodiscard]] static auto ApplyMixed(L a, R b) {
            return a / b;
        }
    };

    struct Mod {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = Integer<T> && requires(T a, T b) {
            {a % b}->std::same_as<T>;
        };

        /// A zero divisor gives 0, and so does `-1` (the remainder is always 0, and the plain operator is undefined for the minimum)
        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if (b == T{0} || b == T{-1}) {
                return T{0};
            }
            return a % b;
        }
    };

    struct Neg {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = requires(T a) {
            {-a}->std::same_as<T>;
        };

        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            if constexpr (Integer<T>) {
                return WrapNegate(a);
            } else if constexpr (std::is_same_v<T, IVec2>) {
                return IVec2{WrapNegate(a.x), WrapNegate(a.y)};
            } else {
                return -a;
            }
        }
    };

    struct Abs {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = Number<T>;

        /// The minimum of a signed integer stays the minimum
        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            if constexpr (Integer<T>) {
                return a < T{0} ? WrapNegate(a) : a;
            } else {
                return std::fabs(a);
            }
        }
    };

    struct Eq {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = EqualityComparable<T>;

        /// Exact and componentwise for the vectors: a NaN component makes two vectors unequal
        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return Equal(a, b);
        }
    };

    struct Ne {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = EqualityComparable<T>;

        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return !Equal(a, b);
        }
    };

    struct Lt {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = Number<T> && requires(T a, T b) {
            {a < b}->std::convertible_to<bool>;
        };

        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return a < b;
        }
    };

    struct Le {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = Number<T> && requires(T a, T b) {
            {a <= b}->std::convertible_to<bool>;
        };

        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return a <= b;
        }
    };

    struct Gt {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = Number<T> && requires(T a, T b) {
            {a > b}->std::convertible_to<bool>;
        };

        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return a > b;
        }
    };

    struct Ge {
        static constexpr OpShape kShape = OpShape::Compare;
        template <typename T>
        static constexpr bool kSupports = Number<T> && requires(T a, T b) {
            {a >= b}->std::convertible_to<bool>;
        };

        template <typename T>
        [[nodiscard]] static bool Apply(T a, T b) {
            return a >= b;
        }
    };

    // `&&` and `||` of the language would also take numbers: the logic of a program is for bool only
    struct And {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = std::is_same_v<T, bool>;

        [[nodiscard]] static bool Apply(bool a, bool b) {
            return a && b;
        }
    };

    struct Or {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = std::is_same_v<T, bool>;

        [[nodiscard]] static bool Apply(bool a, bool b) {
            return a || b;
        }
    };

    struct Not {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = std::is_same_v<T, bool>;

        [[nodiscard]] static bool Apply(bool a) {
            return !a;
        }
    };

    struct Min {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = Number<T>;

        /// A NaN operand gives the other one
        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (std::floating_point<T>) {
                if (a != a) {
                    return b;
                }
                if (b != b) {
                    return a;
                }
            }
            return b < a ? b : a;
        }
    };

    struct Max {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = Number<T>;

        /// A NaN operand gives the other one
        template <typename T>
        [[nodiscard]] static T Apply(T a, T b) {
            if constexpr (std::floating_point<T>) {
                if (a != a) {
                    return b;
                }
                if (b != b) {
                    return a;
                }
            }
            return a < b ? b : a;
        }
    };

    struct Clamp {
        static constexpr OpShape kShape = OpShape::Ternary;
        template <typename T>
        static constexpr bool kSupports = Number<T>;

        /// `min(max(x, lo), hi)`, and `lo` when `lo > hi` (`std::clamp` is undefined there); a NaN bound or value is skipped as in `Min` and `Max`
        template <typename T>
        [[nodiscard]] static T Apply(T x, T lo, T hi) {
            if (lo > hi) {
                return lo;
            }
            return Min::Apply(Max::Apply(x, lo), hi);
        }
    };

    struct Select {
        static constexpr OpShape kShape = OpShape::Select;
        template <typename T>
        static constexpr bool kSupports = true;

        /// Both branches were computed already: a program has no control flow
        template <typename T>
        [[nodiscard]] static T Apply(bool condition, T a, T b) {
            return condition ? a : b;
        }
    };

    /// One channel of `Lerp(Color)`: the engine's rounding and clamp, with a NaN (from a NaN or infinite `t`) as 0, where the engine's cast of it is undefined
    [[nodiscard]] inline unsigned char LerpColorChannel(unsigned char from, unsigned char to, float t) {
        const float value = std::round(std::lerp(static_cast<float>(from), static_cast<float>(to), t));
        if (value != value) {
            return 0;
        }
        return static_cast<unsigned char>(std::clamp(value, 0.0f, kColorChannelMax));
    }

    struct Lerp {
        static constexpr OpShape kShape = OpShape::Blend;
        /// The floats, the types the engine has a `Lerp` for, and `Quat` (`Slerp`)
        template <typename T>
        static constexpr bool kSupports = std::floating_point<T> || std::is_same_v<T, Quat> || (!std::is_arithmetic_v<T> && requires(T a, T b, float t) { {BECore::Lerp(a, b, t)}->std::same_as<T>; });

        /// The type of the third operand: the number itself for a float, `float` for everything else
        template <typename T>
        using Weight = std::conditional_t<std::floating_point<T>, T, float>;

        /// `std::lerp` for a float (exact at 0 and 1, monotonic) and the engine's `Lerp` per component; `Quat` takes the shortest arc (`Slerp`). @p t is not clamped
        template <typename T>
        [[nodiscard]] static T Apply(T from, T to, Weight<T> t) {
            if constexpr (std::floating_point<T>) {
                return std::lerp(from, to, t);
            } else if constexpr (std::is_same_v<T, Quat>) {
                return BECore::Slerp(from, to, t);
            } else if constexpr (std::is_same_v<T, Color>) {
                return Color{LerpColorChannel(from.r, to.r, t), LerpColorChannel(from.g, to.g, t), LerpColorChannel(from.b, to.b, t), LerpColorChannel(from.a, to.a, t)};
            } else {
                return BECore::Lerp(from, to, t);
            }
        }
    };

    struct Floor {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = std::floating_point<T>;

        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            return std::floor(a);
        }
    };

    struct Ceil {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = std::floating_point<T>;

        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            return std::ceil(a);
        }
    };

    struct Round {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = std::floating_point<T>;

        /// To the nearest integer, halves away from zero
        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            return std::round(a);
        }
    };

    struct Sqrt {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = std::floating_point<T>;

        /// IEEE: the root of a negative number is NaN
        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            return std::sqrt(a);
        }
    };

    struct Dot {
        static constexpr OpShape kShape = OpShape::Binary;
        template <typename T>
        static constexpr bool kSupports = requires(T a, T b) {
            {BECore::Dot(a, b)}->std::same_as<float>;
        };

        template <typename T>
        using Result = float;

        template <typename T>
        [[nodiscard]] static float Apply(T a, T b) {
            return BECore::Dot(a, b);
        }
    };

    struct Length {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = requires(T a) {
            {a.Length()}->std::same_as<float>;
        };

        template <typename T>
        using Result = float;

        template <typename T>
        [[nodiscard]] static float Apply(T a) {
            return a.Length();
        }
    };

    struct Normalize {
        static constexpr OpShape kShape = OpShape::Unary;
        template <typename T>
        static constexpr bool kSupports = requires(T a) {
            {a.Normalized()}->std::same_as<T>;
        };

        /// The engine's `Normalized`: a vector of length 0 gives the zero vector, a zero `Quat` the identity, a NaN stays NaN
        template <typename T>
        [[nodiscard]] static T Apply(T a) {
            return a.Normalized();
        }
    };

    namespace Details {

        template <typename Op, typename T>
        consteval auto ResultTag() {
            if constexpr (requires { typename Op::template Result<T>; }) {
                return std::type_identity<typename Op::template Result<T>>{};
            } else if constexpr (Op::kShape == OpShape::Compare) {
                return std::type_identity<bool>{};
            } else {
                return std::type_identity<T>{};
            }
        }

    }  // namespace Details

    /// The C++ type an operation on @p T writes: its own `Result<T>` (`float` for `Dot` and `Length`), `bool` for a comparison, else @p T
    template <typename Op, typename T>
    using ResultOf = typename decltype(Details::ResultTag<Op, T>())::type;

    /// The C++ type an operation on a pair of different types writes
    template <typename Op, typename L, typename R>
    using MixedResultOf = decltype(Op::template ApplyMixed<L, R>(std::declval<L>(), std::declval<R>()));

    /// Runs one typed row of the instruction set: the shape of the operation says which registers it reads and writes
    template <typename Op, typename T>
    inline void Execute(RegisterImage* regs, const Instruction& ins, const Context& context) {
        if constexpr (Op::kShape == OpShape::Slot) {
            const void* object = context.Object(ins.a);
            ASSERT(object != nullptr, "Program: the context has no object at the level the program reads from");
            const uint8_t* field = static_cast<const uint8_t*>(object) + ins.imm;
            if constexpr (std::is_same_v<T, ProgramEnum>) {
                Store<ProgramEnum>(regs[ins.dst], ProgramEnum{LoadEnumIndex(field, ins.b)});
            } else {
                T value{};
                std::memcpy(&value, field, sizeof(T));
                Store<T>(regs[ins.dst], value);
            }
        } else if constexpr (Op::kShape == OpShape::Unary) {
            Store<ResultOf<Op, T>>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a])));
        } else if constexpr (Op::kShape == OpShape::Binary || Op::kShape == OpShape::Compare) {
            Store<ResultOf<Op, T>>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), Load<T>(regs[ins.b])));
        } else if constexpr (Op::kShape == OpShape::Ternary) {
            Store<T>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), Load<T>(regs[ins.b]), Load<T>(regs[ins.c])));
        } else if constexpr (Op::kShape == OpShape::Blend) {
            Store<T>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), Load<T>(regs[ins.b]), Load<typename Op::template Weight<T>>(regs[ins.c])));
        } else {
            Store<T>(regs[ins.dst], Op::Apply(Load<bool>(regs[ins.a]), Load<T>(regs[ins.b]), Load<T>(regs[ins.c])));
        }
    }

    /// Runs one row of `BE_PROGRAM_MIXED_ROWS`: the left operand is in `a`, the right in `b`
    template <typename Op, typename L, typename R>
    inline void ExecuteMixed(RegisterImage* regs, const Instruction& ins) {
        Store<MixedResultOf<Op, L, R>>(regs[ins.dst], Op::ApplyMixed(Load<L>(regs[ins.a]), Load<R>(regs[ins.b])));
    }

    /// Whether `BE_PROGRAM_ROWS` has a row for the operation and the type
    template <typename Op, typename T>
    inline constexpr bool kHasRow = false;

#define BE_PROGRAM_HAS_ROW(Op, Type, Cpp)                                                                                                                                                              \
    template <>                                                                                                                                                                                        \
    inline constexpr bool kHasRow<Op, Cpp> = true;
    BE_PROGRAM_ROWS(BE_PROGRAM_HAS_ROW)
#undef BE_PROGRAM_HAS_ROW

    /// Over every program type: the operation has a kernel exactly where it has a row
#define BE_PROGRAM_ROWS_AGREE(Name, Cpp) &&(Op::template kSupports<Cpp> == kHasRow<Op, Cpp>)
    template <typename Op>
    inline constexpr bool kRowsAgree = true BE_PROGRAM_TYPES(BE_PROGRAM_ROWS_AGREE);
#undef BE_PROGRAM_ROWS_AGREE

#define BE_PROGRAM_CHECK_ROW(Op, Type, Cpp) static_assert(kRowsAgree<Op>, "an operation and its rows in BE_PROGRAM_ROWS disagree about a type: " #Op);
    BE_PROGRAM_ROWS(BE_PROGRAM_CHECK_ROW)
#undef BE_PROGRAM_CHECK_ROW

    /// Whether `BE_PROGRAM_MIXED_ROWS` has a row for the operation and the pair
    template <typename Op, typename L, typename R>
    inline constexpr bool kHasMixedRow = false;

#define BE_PROGRAM_HAS_MIXED_ROW(Op, LName, LCpp, RName, RCpp)                                                                                                                                         \
    template <>                                                                                                                                                                                        \
    inline constexpr bool kHasMixedRow<Op, LCpp, RCpp> = true;
    BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_HAS_MIXED_ROW)
#undef BE_PROGRAM_HAS_MIXED_ROW

    /// Over every pair of program types: a pair is a mixed row exactly when the operation supports it
#define BE_PROGRAM_MIXED_AGREE_RIGHT(LCpp, RName, RCpp) &&(Op::template kSupportsMixed<LCpp, RCpp> == kHasMixedRow<Op, LCpp, RCpp>)
#define BE_PROGRAM_MIXED_AGREE_LEFT(LName, LCpp) BE_PROGRAM_TYPES_ALL(BE_PROGRAM_MIXED_AGREE_RIGHT, LCpp)
    template <typename Op>
    inline constexpr bool kMixedRowsAgree = true BE_PROGRAM_TYPES(BE_PROGRAM_MIXED_AGREE_LEFT);
#undef BE_PROGRAM_MIXED_AGREE_LEFT
#undef BE_PROGRAM_MIXED_AGREE_RIGHT

    static_assert(kMixedRowsAgree<Mul> && kMixedRowsAgree<Div>, "BE_PROGRAM_MIXED_ROWS and the mixed pairs the kernels support disagree");

}  // namespace BECore::Kernel

namespace BECore {

    /// The type an instruction writes to its destination register: a row's result (`bool` for a comparison), a conversion's target; `nullopt` for `LoadConst`, `Copy` and `Invalid`
    [[nodiscard]] constexpr Optional<ProgramType> ResultTypeOf(Opcode op) {
        switch (op) {
#define BE_PROGRAM_RESULT_ROW(Op, Type, Cpp)                                                                                                                                                           \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return kProgramTypeOf<Kernel::ResultOf<Kernel::Op, Cpp>>;
            BE_PROGRAM_ROWS(BE_PROGRAM_RESULT_ROW)
#undef BE_PROGRAM_RESULT_ROW
#define BE_PROGRAM_RESULT_MIXED(Op, LName, LCpp, RName, RCpp)                                                                                                                                          \
    case Opcode::Op##LName##RName:                                                                                                                                                                     \
        return kProgramTypeOf<Kernel::MixedResultOf<Kernel::Op, LCpp, RCpp>>;
            BE_PROGRAM_MIXED_ROWS(BE_PROGRAM_RESULT_MIXED)
#undef BE_PROGRAM_RESULT_MIXED
#define BE_PROGRAM_RESULT_CONVERSION(Src, SrcCpp, Dst, DstCpp)                                                                                                                                         \
    case Opcode::Src##To##Dst:                                                                                                                                                                         \
        return ProgramType::Dst;
            BE_PROGRAM_CONVERSIONS(BE_PROGRAM_RESULT_CONVERSION)
#undef BE_PROGRAM_RESULT_CONVERSION
            case Opcode::Invalid:
            case Opcode::LoadConst:
            case Opcode::Copy:
                break;
        }
        return std::nullopt;
    }

}  // namespace BECore
