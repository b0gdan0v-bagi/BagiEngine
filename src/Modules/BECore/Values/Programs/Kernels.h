#pragma once

#include <BECore/Animation/Lerp.h>
#include <BECore/Assert/AssertMacros.h>
#include <BECore/Utils/EnumUtils.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Domain/ProgramDomain.h>
#include <BECore/Values/Programs/Instruction.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <BECore/Values/Programs/Opcode.h>
#include <algorithm>
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
    CORE_ENUM(OpShape, uint8_t, Slot, Unary, Binary, Compare, Ternary, Blend, Select, Extract, Pack, Element)

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

    /// The field at `imm` of the element in register `a`, as `LoadSlot` reads a field of a context object; a missing element (`ElementImage::ptr` null) gives `T{}`, so a program over a list never
    /// reads outside it. Over an enum `b` is the size of the field in bytes
    struct LoadElem {
        static constexpr OpShape kShape = OpShape::Element;
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

        /// Both branches were computed already: `select` does not branch (only the loops of `Lists/` do)
        template <typename T>
        [[nodiscard]] static T Apply(bool condition, T a, T b) {
            return condition ? a : b;
        }
    };

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

    /// The entry of a table of @p count entries that `LoadTable` takes for @p index: a negative index is the first, one past the end the last (never zero entries: `Finish` refuses)
    [[nodiscard]] constexpr uint32_t TableIndex(int32_t index, uint32_t count) {
        if (index < 0) {
            return 0;
        }
        return std::min(static_cast<uint32_t>(index), count - 1);
    }

    /// One component of a value of a type with components; the index is in the instruction's `imm`
    struct Extract {
        static constexpr OpShape kShape = OpShape::Extract;
        template <typename T>
        static constexpr bool kSupports = HasComponents<T>;

        template <typename T>
        using Result = typename ComponentTraits<T>::Value;

        /// The components are the members one after the other (`Components.h` pins it); an index beyond them (never in a program that passed `Finish`) reads the last one
        template <typename T>
        [[nodiscard]] static Result<T> Apply(const T& value, uint32_t index) {
            using Storage = typename ComponentTraits<T>::Storage;
            const uint32_t at = index < ComponentTraits<T>::kCount ? index : ComponentTraits<T>::kCount - 1;
            Storage component{};
            std::memcpy(&component, reinterpret_cast<const uint8_t*>(&value) + at * sizeof(Storage), sizeof(Storage));
            return static_cast<Result<T>>(component);
        }
    };

    /// A value of a type with components from its components, which are in the registers `a`, `a + 1`, ... of the instruction; the engine's constructors, so nothing is normalized
    struct Pack {
        static constexpr OpShape kShape = OpShape::Pack;
        template <typename T>
        static constexpr bool kSupports = HasComponents<T>;

        /// A `Color` channel is an `int` to a program and a byte in the value: it is clamped to 0..255
        template <typename T>
        [[nodiscard]] static T Apply(const RegisterImage* operands) {
            using Value = typename ComponentTraits<T>::Value;
            constexpr uint32_t kCount = ComponentTraits<T>::kCount;
            Array<Value, kCount> v{};
            for (uint32_t i = 0; i < kCount; ++i) {
                v[i] = Load<Value>(operands[i]);
            }
            if constexpr (std::is_same_v<T, Color>) {
                constexpr auto kChannelMax = static_cast<int32_t>(kColorChannelMax);
                const auto channel = [](int32_t value) { return static_cast<unsigned char>(value < 0 ? 0 : (value > kChannelMax ? kChannelMax : value)); };
                return Color{channel(v[0]), channel(v[1]), channel(v[2]), channel(v[3])};
            } else if constexpr (kCount == 2) {
                return T{v[0], v[1]};
            } else if constexpr (kCount == 3) {
                return T{v[0], v[1], v[2]};
            } else {
                return T{v[0], v[1], v[2], v[3]};
            }
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

    /// The field of type @p T at @p offset of the object of @p level, as `LoadSlot` reads it
    template <typename T>
    [[nodiscard]] inline T LoadField(const Context& context, uint8_t level, uint32_t offset) {
        const void* object = context.Object(level);
        ASSERT(object != nullptr, "Program: the context has no object at the level the program reads from");
        T value{};
        std::memcpy(&value, static_cast<const uint8_t*>(object) + offset, sizeof(T));
        return value;
    }

    /// `LoadSlotDomain`: the @p bytes of the registered type at @p offset of the object of @p level, the rest of the register zero (equal values are equal images)
    inline void LoadDomainField(RegisterImage& dst, const Context& context, uint8_t level, uint32_t offset, uint8_t bytes) {
        const void* object = context.Object(level);
        ASSERT(object != nullptr, "Program: the context has no object at the level the program reads from");
        ASSERT(bytes >= 1 && bytes <= kProgramRegisterBytes, "Program: a registered type is 1 to one register of bytes");
        dst = RegisterImage{};
        std::memcpy(dst.bytes.data(), static_cast<const uint8_t*>(object) + offset, std::min<size_t>(bytes, kProgramRegisterBytes));
    }

    /// Runs one typed row of the instruction set: the shape of the operation says which registers it reads and writes
    template <typename Op, typename T>
    inline void Execute(RegisterImage* regs, const Instruction& ins, const Context& context) {
        if constexpr (Op::kShape == OpShape::Slot) {
            if constexpr (std::is_same_v<T, ProgramEnum>) {
                const void* object = context.Object(ins.a);
                ASSERT(object != nullptr, "Program: the context has no object at the level the program reads from");
                Store<ProgramEnum>(regs[ins.dst], ProgramEnum{LoadEnumIndex(static_cast<const uint8_t*>(object) + ins.imm, ins.b)});
            } else if constexpr (std::is_same_v<T, ProgramDomainValue>) {
                LoadDomainField(regs[ins.dst], context, ins.a, ins.imm, ins.b);
            } else {
                Store<T>(regs[ins.dst], LoadField<T>(context, ins.a, ins.imm));
            }
        } else if constexpr (Op::kShape == OpShape::Element) {
            const auto element = Load<ElementImage>(regs[ins.a]);
            if constexpr (std::is_same_v<T, ProgramEnum>) {
                Store<ProgramEnum>(regs[ins.dst], ProgramEnum{element.ptr != nullptr ? LoadEnumIndex(element.ptr + ins.imm, ins.b) : 0U});
            } else if constexpr (std::is_same_v<T, ProgramDomainValue>) {
                // As `LoadDomainField`: only the type's own bytes, the rest of the register zero; a missing element is the zero image
                ASSERT(ins.b >= 1 && ins.b <= kProgramRegisterBytes, "Program: a registered type is 1 to one register of bytes");
                regs[ins.dst] = RegisterImage{};
                if (element.ptr != nullptr) {
                    std::memcpy(regs[ins.dst].bytes.data(), element.ptr + ins.imm, std::min<size_t>(ins.b, kProgramRegisterBytes));
                }
            } else {
                T value{};
                if (element.ptr != nullptr) {
                    std::memcpy(&value, element.ptr + ins.imm, sizeof(T));
                }
                Store<T>(regs[ins.dst], value);
            }
        } else if constexpr (Op::kShape == OpShape::Unary) {
            Store<ResultOf<Op, T>>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a])));
        } else if constexpr (Op::kShape == OpShape::Binary || Op::kShape == OpShape::Compare) {
            Store<ResultOf<Op, T>>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), Load<T>(regs[ins.b])));
        } else if constexpr (Op::kShape == OpShape::Ternary) {
            Store<T>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), Load<T>(regs[ins.b]), Load<T>(regs[ins.c])));
        } else if constexpr (Op::kShape == OpShape::Extract) {
            Store<ResultOf<Op, T>>(regs[ins.dst], Op::Apply(Load<T>(regs[ins.a]), ins.imm));
        } else if constexpr (Op::kShape == OpShape::Pack) {
            Store<T>(regs[ins.dst], Op::template Apply<T>(&regs[ins.a]));
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

    /// Runs `MulAddSlot<T>`: the field is read where the unfused code read it and the result is `Mul` then `Add` of the same kernels. The product is a variable of its own so
    /// that no compiler contracts the two into one fused multiply-add, which rounds once and differs in the last bit from the two instructions. Over `Vec3` the factor is a
    /// `float` constant (the mixed `Mul`) and the addend a `Vec3` constant.
    template <typename T>
    inline void ExecuteMulAddSlot(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants, const Context& context) {
        static_assert(Number<T> || std::is_same_v<T, Vec3>, "MulAddSlot has rows for the numbers and Vec3");
        ASSERT(ins.b < constants.size() && ins.c < constants.size());
        const T field = LoadField<T>(context, ins.a, ins.imm);
        if constexpr (std::is_same_v<T, Vec3>) {
            const Vec3 product = Mul::ApplyMixed(field, Load<float>(constants[ins.b]));
            Store<Vec3>(regs[ins.dst], Add::Apply(product, Load<Vec3>(constants[ins.c])));
        } else {
            const T product = Mul::Apply(field, Load<T>(constants[ins.b]));
            Store<T>(regs[ins.dst], Add::Apply(product, Load<T>(constants[ins.c])));
        }
    }

    /// Runs `LerpSlots<T>`: `Lerp` of the two fields and the weight in register `c`, so the result is the one of the typed row
    template <std::floating_point T>
    inline void ExecuteLerpSlots(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> /*constants*/, const Context& context) {
        const T from = LoadField<T>(context, ins.a, LowHalf(ins.imm));
        const T to = LoadField<T>(context, ins.b, HighHalf(ins.imm));
        Store<T>(regs[ins.dst], Lerp::Apply(from, to, Load<T>(regs[ins.c])));
    }

    /// Runs `LoadConst`: the constant at `imm`, whatever its type
    inline void ExecuteLoadConst(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants) {
        ASSERT(ins.imm < constants.size());
        regs[ins.dst] = constants[ins.imm];
    }

    /// Runs `Copy`: register `a` into `dst`
    inline void ExecuteCopy(RegisterImage* regs, const Instruction& ins) {
        regs[ins.dst] = regs[ins.a];
    }

    /// Runs `LoadTable`: the entry of the run of constants that `TableIndex` picks for the `int32_t` in register `a`
    inline void ExecuteLoadTable(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants) {
        const uint32_t count = TableEntryCount(ins);
        ASSERT(count > 0 && ins.imm + count <= constants.size());
        regs[ins.dst] = constants[ins.imm + TableIndex(Load<int32_t>(regs[ins.a]), count)];
    }

    /// Runs `LoadTableSlot`: `LoadTable` of an `int32_t` field read where `LoadSlot` reads it (the offset in the low half of `imm`, the first entry in the high half)
    inline void ExecuteLoadTableSlot(RegisterImage* regs, const Instruction& ins, Span<const RegisterImage> constants, const Context& context) {
        const uint32_t count = TableEntryCount(ins);
        const uint32_t first = HighHalf(ins.imm);
        ASSERT(count > 0 && first + count <= constants.size());
        regs[ins.dst] = constants[first + TableIndex(LoadField<int32_t>(context, ins.a, LowHalf(ins.imm)), count)];
    }

    /// Runs `CallDomain`: the operation of the game that `imm` names, through the plain function pointer of the registry
    inline void ExecuteCallDomain(RegisterImage* regs, const Instruction& ins, const Context& context) {
        ProgramDomain::Operation(ins.imm).function(regs, ins, context);
    }

    /// Writes the result of a program, `ProgramTypeSize(type)` bytes of register @p resultRegister, to @p result
    inline void StoreResult(const RegisterImage* regs, uint8_t resultRegister, ProgramType type, void* result) {
        std::memcpy(result, regs[resultRegister].bytes.data(), ProgramTypeSize(type));
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

    /// The type an instruction writes to its destination register: a row's result (`bool` for a comparison), a conversion's target; `nullopt` for `LoadConst`, `Copy`, `Invalid` and the instructions
    /// that write a list or an element register or none (`ListSize` gives `Int32`)
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
#define BE_PROGRAM_RESULT_FUSED(Op, Type, Cpp)                                                                                                                                                         \
    case Opcode::Op##Type:                                                                                                                                                                             \
        return ProgramType::Type;
            BE_PROGRAM_FUSED_ROWS(BE_PROGRAM_RESULT_FUSED)
#undef BE_PROGRAM_RESULT_FUSED
            case Opcode::Invalid:
            case Opcode::LoadConst:
            case Opcode::Copy:
            case Opcode::LoadTable:
            case Opcode::LoadTableSlot:
            case Opcode::CallDomain:
            case Opcode::ListSlot:
            case Opcode::ListElem:
            case Opcode::ListAt:
            case Opcode::LoopBegin:
            case Opcode::LoopEnd:
            case Opcode::BreakIf:
                break;
            case Opcode::ListSize:
                return ProgramType::Int32;
        }
        return std::nullopt;
    }

}  // namespace BECore

// The kernels of the list instructions come last: they use `Load`, `Store` and the registers above, and a generated file includes this header only
#include <BECore/Values/Programs/Lists/ListKernels.h>
