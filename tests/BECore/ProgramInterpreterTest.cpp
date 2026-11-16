// The instruction set and the interpreter of programs (Values/Programs/): every row of BE_PROGRAM_ROWS and BE_PROGRAM_CONVERSIONS against a plain
// C++ reference over edge inputs, the rules of the operations by number, the program pool and the builder. The programs are assembled by hand.

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Printers.h>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
        constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();
        constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::min();
        constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        constexpr float kInf = std::numeric_limits<float>::infinity();
        constexpr double kDoubleNaN = std::numeric_limits<double>::quiet_NaN();
        constexpr double kDoubleInf = std::numeric_limits<double>::infinity();
        constexpr double kFloatOverflowThreshold = 0x1.ffffffp127;
        constexpr int kThreadCount = 4;
        constexpr int kProgramsPerThread = 200;

        template <typename T>
        Vector<T> Edges();

        template <>
        Vector<bool> Edges<bool>() {
            return {false, true};
        }

        template <>
        Vector<int32_t> Edges<int32_t>() {
            return {0, 1, -1, 2, -2, 7, -7, 100, 65536, kInt32Min, kInt32Min + 1, kInt32Max, kInt32Max - 1};
        }

        template <>
        Vector<int64_t> Edges<int64_t>() {
            return {0, 1, -1, 2, -2, 7, -7, 100, 4294967296, kInt64Min, kInt64Min + 1, kInt64Max, kInt64Max - 1, static_cast<int64_t>(kInt32Min), static_cast<int64_t>(kInt32Max)};
        }

        template <>
        Vector<float> Edges<float>() {
            return {0.0f, 1.0f,  -1.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 3.0f, -3.25f, std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::min(),
                    kInf, -kInf, kNaN};
        }

        template <>
        Vector<double> Edges<double>() {
            return {0.0,
                    1.0,
                    -1.0,
                    0.5,
                    -0.5,
                    1.5,
                    -1.5,
                    2.5,
                    -2.5,
                    3.0,
                    -3.25,
                    std::numeric_limits<double>::max(),
                    std::numeric_limits<double>::lowest(),
                    std::numeric_limits<double>::min(),
                    kDoubleInf,
                    -kDoubleInf,
                    kDoubleNaN};
        }

        /// What a conversion sees besides the edges of its source: the values around the range of each target and the threshold at which a `double` is out of `float`
        template <typename T>
        Vector<T> ConversionEdges() {
            Vector<T> values = Edges<T>();
            if constexpr (std::floating_point<T>) {
                for (const double value : {2147483647.0,
                                           2147483648.0,
                                           -2147483648.0,
                                           -2147483649.0,
                                           4294967296.5,
                                           9223372036854775807.0,
                                           9223372036854775808.0,
                                           -9223372036854775808.0,
                                           1e19,
                                           -1e19,
                                           1e39,
                                           -1e39,
                                           1e300,
                                           -1e300,
                                           0.1,
                                           3.99,
                                           -3.99,
                                           4.9e-324,
                                           kFloatOverflowThreshold,
                                           std::nextafter(kFloatOverflowThreshold, 0.0),
                                           -kFloatOverflowThreshold,
                                           static_cast<double>(std::numeric_limits<float>::max())}) {
                    if (std::fabs(value) <= static_cast<double>(std::numeric_limits<T>::max())) {
                        values.push_back(static_cast<T>(value));
                    }
                }
                values.push_back(static_cast<T>(2147483520.0));
            }
            return values;
        }

        template <typename T>
        bool Same(T a, T b) {
            if constexpr (std::floating_point<T>) {
                return (a != a && b != b) || a == b;
            } else {
                return a == b;
            }
        }

        template <typename T>
        T WrapTo(uint64_t word) {
            return static_cast<T>(static_cast<std::make_unsigned_t<T>>(word));
        }

        template <typename T>
        uint64_t Word(T value) {
            return static_cast<uint64_t>(static_cast<int64_t>(value));
        }

        /// The kernels of the operations with their own names, so a reference below can say which operation it is
        namespace K = Kernel;

        template <typename T>
        bool IsNan(T value) {
            return value != value;
        }

        /// The plain C++ meaning of every operation, written another way than the kernel: integers through 64-bit words, the rules of the stage plan spelled out
        template <typename Op, typename T>
        auto Reference(T a, T b = T{}, T c = T{}) {
            if constexpr (std::is_same_v<Op, K::Add>) {
                if constexpr (std::floating_point<T>) {
                    return a + b;
                } else {
                    return WrapTo<T>(Word(a) + Word(b));
                }
            } else if constexpr (std::is_same_v<Op, K::Sub>) {
                if constexpr (std::floating_point<T>) {
                    return a - b;
                } else {
                    return WrapTo<T>(Word(a) - Word(b));
                }
            } else if constexpr (std::is_same_v<Op, K::Mul>) {
                if constexpr (std::floating_point<T>) {
                    return a * b;
                } else {
                    return WrapTo<T>(Word(a) * Word(b));
                }
            } else if constexpr (std::is_same_v<Op, K::Div>) {
                if constexpr (std::floating_point<T>) {
                    return a / b;
                } else if (b == 0) {
                    return T{0};
                } else if (a == std::numeric_limits<T>::min() && b == -1) {
                    return a;
                } else {
                    return static_cast<T>(a / b);
                }
            } else if constexpr (std::is_same_v<Op, K::Mod>) {
                if (b == 0 || b == -1) {
                    return T{0};
                }
                return static_cast<T>(a % b);
            } else if constexpr (std::is_same_v<Op, K::Neg>) {
                if constexpr (std::floating_point<T>) {
                    return -a;
                } else {
                    return WrapTo<T>(Word(T{0}) - Word(a));
                }
            } else if constexpr (std::is_same_v<Op, K::Abs>) {
                if constexpr (std::floating_point<T>) {
                    return std::fabs(a);
                } else {
                    return a < 0 ? WrapTo<T>(Word(T{0}) - Word(a)) : a;
                }
            } else if constexpr (std::is_same_v<Op, K::Eq>) {
                return a == b;
            } else if constexpr (std::is_same_v<Op, K::Ne>) {
                return a != b;
            } else if constexpr (std::is_same_v<Op, K::Lt>) {
                return a < b;
            } else if constexpr (std::is_same_v<Op, K::Le>) {
                return a <= b;
            } else if constexpr (std::is_same_v<Op, K::Gt>) {
                return a > b;
            } else if constexpr (std::is_same_v<Op, K::Ge>) {
                return a >= b;
            } else if constexpr (std::is_same_v<Op, K::And>) {
                return a && b;
            } else if constexpr (std::is_same_v<Op, K::Or>) {
                return a || b;
            } else if constexpr (std::is_same_v<Op, K::Not>) {
                return !a;
            } else if constexpr (std::is_same_v<Op, K::Min>) {
                if (IsNan(a)) {
                    return b;
                }
                return IsNan(b) || a < b ? a : b;
            } else if constexpr (std::is_same_v<Op, K::Max>) {
                if (IsNan(a)) {
                    return b;
                }
                return IsNan(b) || a > b ? a : b;
            } else if constexpr (std::is_same_v<Op, K::Clamp>) {
                if (b > c) {
                    return b;
                }
                T value = a;
                if (!IsNan(b) && (IsNan(value) || value < b)) {
                    value = b;
                }
                if (!IsNan(c) && (IsNan(value) || value > c)) {
                    value = c;
                }
                return value;
            } else if constexpr (std::is_same_v<Op, K::Lerp>) {
                return std::lerp(a, b, c);
            } else if constexpr (std::is_same_v<Op, K::Floor>) {
                return std::floor(a);
            } else if constexpr (std::is_same_v<Op, K::Ceil>) {
                return std::ceil(a);
            } else if constexpr (std::is_same_v<Op, K::Round>) {
                return std::round(a);
            } else {
                static_assert(std::is_same_v<Op, K::Sqrt>, "a row without a reference");
                return std::sqrt(a);
            }
        }

        /// Runs one instruction over constants: the operands are loaded into the first registers, the result is read from the next one
        template <typename R, typename... Args>
        R RunOpcode(Opcode op, Args... args) {
            ProgramBuilder builder;
            static_assert(sizeof...(Args) <= 3);
            Array<uint8_t, 3> registers{};
            size_t index = 0;
            const auto load = [&](auto value) {
                registers[index] = builder.AcquireRegister().value();
                builder.EmitLoadConst(registers[index], value);
                ++index;
            };
            (load(args), ...);
            const uint8_t dst = builder.AcquireRegister().value();
            builder.Emit(op, dst, registers[0], registers[1], registers[2]);
            const auto program = builder.Finish(kProgramTypeOf<R>, dst);
            EXPECT_TRUE(program.has_value());
            if (!program.has_value()) {
                return R{};
            }
            return Run<R>(*program, Context{});
        }

        template <typename T>
        String Show(T value) {
            return Format("{}", value);
        }

        int g_rowsChecked = 0;

        template <typename Op, typename T>
        void CheckRow(Opcode op) {
            ++g_rowsChecked;
            using Result = std::conditional_t<Op::kShape == K::OpShape::Compare, bool, T>;
            const String name(EnumUtils<Opcode>::ToString(op));
            if constexpr (Op::kShape == K::OpShape::Slot) {
                return;  // SlotLoadsReadEveryType
            } else if constexpr (Op::kShape == K::OpShape::Unary) {
                for (const T a : Edges<T>()) {
                    const Result got = RunOpcode<Result>(op, a);
                    EXPECT_TRUE(Same<Result>(got, Reference<Op, T>(a))) << name << "(" << Show(a) << ") gave " << Show(got) << ", the reference " << Show(Reference<Op, T>(a));
                }
            } else if constexpr (Op::kShape == K::OpShape::Binary || Op::kShape == K::OpShape::Compare) {
                for (const T a : Edges<T>()) {
                    for (const T b : Edges<T>()) {
                        const Result got = RunOpcode<Result>(op, a, b);
                        EXPECT_TRUE(Same<Result>(got, Reference<Op, T>(a, b)))
                            << name << "(" << Show(a) << ", " << Show(b) << ") gave " << Show(got) << ", the reference " << Show(Reference<Op, T>(a, b));
                    }
                }
            } else if constexpr (Op::kShape == K::OpShape::Ternary) {
                for (const T a : Edges<T>()) {
                    for (const T b : Edges<T>()) {
                        for (const T c : Edges<T>()) {
                            const Result got = RunOpcode<Result>(op, a, b, c);
                            EXPECT_TRUE(Same<Result>(got, Reference<Op, T>(a, b, c)))
                                << name << "(" << Show(a) << ", " << Show(b) << ", " << Show(c) << ") gave " << Show(got) << ", the reference " << Show(Reference<Op, T>(a, b, c));
                        }
                    }
                }
            } else {
                for (const bool condition : Edges<bool>()) {
                    for (const T a : Edges<T>()) {
                        for (const T b : Edges<T>()) {
                            const T got = RunOpcode<T>(op, condition, a, b);
                            EXPECT_TRUE(Same<T>(got, condition ? a : b)) << name << " of " << condition << " gave " << Show(got);
                        }
                    }
                }
            }
        }

        template <typename From, typename To>
        To ReferenceConvert(From value) {
            if constexpr (std::floating_point<From> && std::is_integral_v<To>) {
                constexpr double kBound = std::numeric_limits<To>::digits >= 63 ? 9223372036854775808.0 : 2147483648.0;
                if (IsNan(value)) {
                    return To{0};
                }
                const double truncated = std::trunc(static_cast<double>(value));
                if (truncated >= kBound) {
                    return std::numeric_limits<To>::max();
                }
                if (truncated <= -kBound) {
                    return std::numeric_limits<To>::lowest();
                }
                return static_cast<To>(truncated);
            } else if constexpr (std::is_same_v<From, double> && std::is_same_v<To, float>) {
                if (IsNan(value)) {
                    return kNaN;
                }
                if (std::fabs(value) >= kFloatOverflowThreshold) {
                    return value < 0 ? -kInf : kInf;
                }
                return static_cast<float>(value);
            } else if constexpr (std::is_integral_v<From> && std::is_integral_v<To>) {
                return WrapTo<To>(Word(value));
            } else {
                return static_cast<To>(value);
            }
        }

        template <typename From, typename To>
        void CheckConversion(Opcode op) {
            ++g_rowsChecked;
            const String name(EnumUtils<Opcode>::ToString(op));
            for (const From value : ConversionEdges<From>()) {
                const To got = RunOpcode<To>(op, value);
                EXPECT_TRUE(Same<To>(got, ReferenceConvert<From, To>(value))) << name << "(" << Show(value) << ") gave " << Show(got) << ", the reference " << Show(ReferenceConvert<From, To>(value));
            }
        }

        template <typename R, typename... Args>
        R Eval(Opcode op, Args... args) {
            return RunOpcode<R>(op, args...);
        }

        /// A program with a constant result: programs that differ only in it are different programs
        Program ConstantProgram(int32_t value) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadConst(reg, value);
            return builder.Finish(ProgramType::Int32, reg).value();
        }

        SlotRef Link(const ValueWorld& world, StringView path, const TypeDesc& type) {
            const auto slot = LinkSlot(world.declaration, path, type);
            EXPECT_TRUE(slot.has_value()) << String(path).c_str();
            return slot.value_or(SlotRef{});
        }

    }  // namespace

    TEST(ProgramInterpreterTest, EveryRowMatchesItsReference) {
        g_rowsChecked = 0;
#define BE_CHECK_ROW(Op, Type, Cpp) CheckRow<Kernel::Op, Cpp>(Opcode::Op##Type);
        BE_PROGRAM_ROWS(BE_CHECK_ROW)
#undef BE_CHECK_ROW
#define BE_CHECK_CONVERSION(Src, SrcCpp, Dst, DstCpp) CheckConversion<SrcCpp, DstCpp>(Opcode::Src##To##Dst);
        BE_PROGRAM_CONVERSIONS(BE_CHECK_CONVERSION)
#undef BE_CHECK_CONVERSION
        // Every opcode but the three untyped ones is a row or a conversion: a row that the matrix skipped would leave the count short
        EXPECT_EQ(static_cast<size_t>(g_rowsChecked) + 3, EnumUtils<Opcode>::Count());
    }

    TEST(ProgramInterpreterTest, IntegerArithmeticWraps) {
        EXPECT_EQ(Eval<int32_t>(Opcode::AddInt32, kInt32Max, 1), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::SubInt32, kInt32Min, 1), kInt32Max);
        EXPECT_EQ(Eval<int32_t>(Opcode::MulInt32, 65536, 65536), 0);
        EXPECT_EQ(Eval<int32_t>(Opcode::MulInt32, kInt32Max, 2), -2);
        EXPECT_EQ(Eval<int32_t>(Opcode::NegInt32, kInt32Min), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::AbsInt32, kInt32Min), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::AbsInt32, -5), 5);
        EXPECT_EQ(Eval<int64_t>(Opcode::AddInt64, kInt64Max, int64_t{1}), kInt64Min);
        EXPECT_EQ(Eval<int64_t>(Opcode::MulInt64, kInt64Min, int64_t{-1}), kInt64Min);
        EXPECT_EQ(Eval<int64_t>(Opcode::NegInt64, kInt64Min), kInt64Min);
    }

    TEST(ProgramInterpreterTest, IntegerDivisionIsTotal) {
        EXPECT_EQ(Eval<int32_t>(Opcode::DivInt32, 7, 0), 0);
        EXPECT_EQ(Eval<int32_t>(Opcode::ModInt32, 7, 0), 0);
        EXPECT_EQ(Eval<int32_t>(Opcode::DivInt32, kInt32Min, -1), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::ModInt32, kInt32Min, -1), 0);
        EXPECT_EQ(Eval<int32_t>(Opcode::DivInt32, -7, 2), -3) << "toward zero";
        EXPECT_EQ(Eval<int32_t>(Opcode::ModInt32, -7, 2), -1) << "the sign of the dividend";
        EXPECT_EQ(Eval<int64_t>(Opcode::DivInt64, kInt64Min, int64_t{-1}), kInt64Min);
        EXPECT_EQ(Eval<int64_t>(Opcode::ModInt64, kInt64Min, int64_t{-1}), 0);
        EXPECT_EQ(Eval<int64_t>(Opcode::DivInt64, int64_t{5}, int64_t{0}), 0);
    }

    TEST(ProgramInterpreterTest, FloatsFollowIeee) {
        EXPECT_TRUE(std::isinf(Eval<float>(Opcode::DivFloat, 1.0f, 0.0f)));
        EXPECT_TRUE(std::isnan(Eval<float>(Opcode::DivFloat, 0.0f, 0.0f)));
        EXPECT_TRUE(std::isnan(Eval<double>(Opcode::SqrtDouble, -1.0)));
        EXPECT_FALSE(Eval<bool>(Opcode::EqFloat, kNaN, kNaN));
        EXPECT_TRUE(Eval<bool>(Opcode::NeFloat, kNaN, kNaN));
        EXPECT_FALSE(Eval<bool>(Opcode::LtDouble, kDoubleNaN, 1.0));
        EXPECT_EQ(Eval<float>(Opcode::RoundFloat, 2.5f), 3.0f) << "halves go away from zero";
        EXPECT_EQ(Eval<float>(Opcode::RoundFloat, -2.5f), -3.0f);
        EXPECT_EQ(Eval<double>(Opcode::FloorDouble, -0.5), -1.0);
        EXPECT_EQ(Eval<double>(Opcode::CeilDouble, -0.5), 0.0);
        EXPECT_EQ(Eval<float>(Opcode::LerpFloat, 2.0f, 6.0f, 0.25f), 3.0f);
        EXPECT_EQ(Eval<float>(Opcode::LerpFloat, 2.0f, 6.0f, 0.0f), 2.0f);
        EXPECT_EQ(Eval<float>(Opcode::LerpFloat, 2.0f, 6.0f, 1.0f), 6.0f);
        EXPECT_EQ(Eval<float>(Opcode::LerpFloat, 2.0f, 6.0f, 2.0f), 10.0f) << "t is not clamped";
    }

    TEST(ProgramInterpreterTest, MinMaxAndClampSkipNan) {
        EXPECT_EQ(Eval<float>(Opcode::MinFloat, kNaN, 3.0f), 3.0f);
        EXPECT_EQ(Eval<float>(Opcode::MinFloat, 3.0f, kNaN), 3.0f);
        EXPECT_EQ(Eval<double>(Opcode::MaxDouble, kDoubleNaN, -3.0), -3.0);
        EXPECT_EQ(Eval<double>(Opcode::MaxDouble, -3.0, kDoubleNaN), -3.0);
        EXPECT_TRUE(std::isnan(Eval<float>(Opcode::MaxFloat, kNaN, kNaN)));
        EXPECT_EQ(Eval<float>(Opcode::ClampFloat, kNaN, 1.0f, 5.0f), 1.0f);
        EXPECT_EQ(Eval<float>(Opcode::ClampFloat, 9.0f, 1.0f, kNaN), 9.0f);
        EXPECT_EQ(Eval<float>(Opcode::ClampFloat, 9.0f, kNaN, 5.0f), 5.0f);
    }

    TEST(ProgramInterpreterTest, ClampWithLowAboveHighIsLow) {
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, 5, 10, 2), 10);
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, 50, 10, 2), 10);
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, 1, 10, 2), 10);
        EXPECT_EQ(Eval<int64_t>(Opcode::ClampInt64, int64_t{5}, int64_t{10}, int64_t{2}), 10);
        EXPECT_EQ(Eval<float>(Opcode::ClampFloat, 5.0f, 10.0f, 2.0f), 10.0f);
        EXPECT_EQ(Eval<double>(Opcode::ClampDouble, 5.0, 10.0, 2.0), 10.0);
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, 5, 2, 10), 5);
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, -5, 2, 10), 2);
        EXPECT_EQ(Eval<int32_t>(Opcode::ClampInt32, 50, 2, 10), 10);
    }

    TEST(ProgramInterpreterTest, FloatToIntegerTruncatesAndSaturates) {
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, 3.99f), 3);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, -3.99f), -3);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, kNaN), 0);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, kInf), kInt32Max);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, -kInf), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, 3.0e9f), kInt32Max);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, 2147483648.0f), kInt32Max);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, -2147483648.0f), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::FloatToInt32, 2147483520.0f), 2147483520);
        EXPECT_EQ(Eval<int32_t>(Opcode::DoubleToInt32, 2147483647.9), kInt32Max);
        EXPECT_EQ(Eval<int32_t>(Opcode::DoubleToInt32, -2147483648.9), kInt32Min);
        EXPECT_EQ(Eval<int32_t>(Opcode::DoubleToInt32, kDoubleNaN), 0);
        EXPECT_EQ(Eval<int64_t>(Opcode::DoubleToInt64, 1e19), kInt64Max);
        EXPECT_EQ(Eval<int64_t>(Opcode::DoubleToInt64, -1e19), kInt64Min);
        EXPECT_EQ(Eval<int64_t>(Opcode::DoubleToInt64, 9223372036854774784.0), 9223372036854774784);
        EXPECT_EQ(Eval<int64_t>(Opcode::FloatToInt64, 4.5e9f), 4499999744);
        EXPECT_EQ(Eval<int64_t>(Opcode::FloatToInt64, kNaN), 0);
    }

    TEST(ProgramInterpreterTest, OtherConversions) {
        EXPECT_EQ(Eval<int32_t>(Opcode::Int64ToInt32, int64_t{4294967297}), 1) << "the low 32 bits";
        EXPECT_EQ(Eval<int32_t>(Opcode::Int64ToInt32, int64_t{2147483648}), kInt32Min);
        EXPECT_EQ(Eval<int64_t>(Opcode::Int32ToInt64, int32_t{-3}), -3);
        EXPECT_EQ(Eval<float>(Opcode::Int32ToFloat, 16777217), 16777216.0f) << "rounds to nearest even";
        EXPECT_EQ(Eval<double>(Opcode::Int64ToDouble, int64_t{9007199254740993}), 9007199254740992.0);
        EXPECT_TRUE(std::isinf(Eval<float>(Opcode::DoubleToFloat, 1e39)));
        EXPECT_EQ(Eval<float>(Opcode::DoubleToFloat, -1e39), -kInf);
        EXPECT_EQ(Eval<float>(Opcode::DoubleToFloat, static_cast<double>(std::numeric_limits<float>::max())), std::numeric_limits<float>::max());
        EXPECT_TRUE(std::isnan(Eval<float>(Opcode::DoubleToFloat, kDoubleNaN)));
        EXPECT_EQ(Eval<double>(Opcode::FloatToDouble, 0.5f), 0.5);
    }

    TEST(ProgramInterpreterTest, LogicAndSelect) {
        EXPECT_TRUE(Eval<bool>(Opcode::AndBool, true, true));
        EXPECT_FALSE(Eval<bool>(Opcode::AndBool, true, false));
        EXPECT_TRUE(Eval<bool>(Opcode::OrBool, false, true));
        EXPECT_TRUE(Eval<bool>(Opcode::NotBool, false));
        EXPECT_EQ(Eval<int32_t>(Opcode::SelectInt32, true, 1, 2), 1);
        EXPECT_EQ(Eval<int32_t>(Opcode::SelectInt32, false, 1, 2), 2);
        EXPECT_TRUE(Eval<bool>(Opcode::SelectBool, false, false, true));
        EXPECT_EQ(Eval<double>(Opcode::SelectDouble, true, 0.5, 1.5), 0.5);
    }

    TEST(ProgramInterpreterTest, SlotLoadsReadEveryType) {
        ValueWorld world;
        world.caster.alive = false;
        world.caster.attack = -41;
        world.caster.score = 5000000000;
        world.caster.speed = 2.75f;
        world.caster.precise = -0.125;

        const auto load = [&](ProgramType type, StringView path, const TypeDesc& desc, auto expected) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadSlot(reg, type, Link(world, path, desc));
            const Program program = builder.Finish(type, reg).value();
            using T = decltype(expected);
            EXPECT_EQ(Run<T>(program, world.context), expected) << String(path).c_str();
        };
        load(ProgramType::Bool, "caster.alive", DescribeType<bool>(), false);
        load(ProgramType::Int32, "caster.attack", DescribeType<int32_t>(), int32_t{-41});
        load(ProgramType::Int64, "caster.score", DescribeType<int64_t>(), int64_t{5000000000});
        load(ProgramType::Float, "caster.speed", DescribeType<float>(), 2.75f);
        load(ProgramType::Double, "caster.precise", DescribeType<double>(), -0.125);
        load(ProgramType::Int32, "cast.level", DescribeType<int32_t>(), int32_t{3});
        load(ProgramType::Int32, "caster.span.high", DescribeType<int32_t>(), int32_t{9});
    }

    TEST(ProgramInterpreterTest, AFormulaOverTwoLevelsAndItsReads) {
        ValueWorld world;
        // caster.attack * (100 + cast.level * 10) / 100
        ProgramBuilder builder;
        const uint8_t attack = builder.AcquireRegister().value();
        builder.EmitLoadSlot(attack, ProgramType::Int32, Link(world, "caster.attack", DescribeType<int32_t>()));
        const uint8_t hundred = builder.AcquireRegister().value();
        builder.EmitLoadConst(hundred, 100);
        const uint8_t level = builder.AcquireRegister().value();
        builder.EmitLoadSlot(level, ProgramType::Int32, Link(world, "cast.level", DescribeType<int32_t>()));
        const uint8_t ten = builder.AcquireRegister().value();
        builder.EmitLoadConst(ten, 10);
        builder.Emit(Opcode::MulInt32, level, level, ten);
        builder.ReleaseRegister(ten);
        builder.Emit(Opcode::AddInt32, hundred, hundred, level);
        builder.ReleaseRegister(level);
        builder.Emit(Opcode::MulInt32, attack, attack, hundred);
        builder.ReleaseRegister(hundred);
        const uint8_t divisor = builder.AcquireRegister().value();
        builder.EmitLoadConst(divisor, 100);
        builder.Emit(Opcode::DivInt32, attack, attack, divisor);
        const Program program = builder.Finish(ProgramType::Int32, attack).value();

        for (const int32_t attackValue : {0, 12, 100, -50}) {
            for (const int32_t levelValue : {0, 3, 10}) {
                world.caster.attack = attackValue;
                world.cast.level = levelValue;
                EXPECT_EQ(Run<int32_t>(program, world.context), attackValue * (100 + levelValue * 10) / 100);
            }
        }
        EXPECT_EQ(program.Reads(), LevelBit(0) | LevelBit(1));
        EXPECT_EQ(program.RegisterCount(), 4);
    }

    TEST(ProgramInterpreterTest, ReadsAreTheLevelsTheLoadsName) {
        ValueWorld world;
        const auto reads = [&](StringView path) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadSlot(reg, ProgramType::Int32, Link(world, path, DescribeType<int32_t>()));
            return builder.Finish(ProgramType::Int32, reg).value().Reads();
        };
        EXPECT_EQ(reads("caster.attack"), LevelBit(0));
        EXPECT_EQ(reads("cast.level"), LevelBit(1));
        EXPECT_EQ(ConstantProgram(5).Reads(), LevelMask{0});
    }

    TEST(ProgramInterpreterTest, TheResultIsTheRegisterNamed) {
        ProgramBuilder builder;
        const uint8_t first = builder.AcquireRegister().value();
        const uint8_t second = builder.AcquireRegister().value();
        builder.EmitLoadConst(first, 11);
        builder.EmitLoadConst(second, 22);
        builder.Emit(Opcode::Copy, first, second);
        EXPECT_EQ(Run<int32_t>(builder.Finish(ProgramType::Int32, first).value(), Context{}), 22);
        EXPECT_EQ(Run<int32_t>(builder.Finish(ProgramType::Int32, second).value(), Context{}), 22);
    }

    TEST(ProgramInterpreterTest, TheBuilderRefusesMoreThanThirtyTwoRegisters) {
        ProgramBuilder builder;
        for (size_t i = 0; i < kMaxProgramRegisters; ++i) {
            ASSERT_TRUE(builder.AcquireRegister().has_value()) << i;
        }
        const auto refused = builder.AcquireRegister();
        ASSERT_FALSE(refused.has_value());
        EXPECT_NE(refused.error().message.find("32 registers"), String::npos) << refused.error().message;
        builder.ReleaseRegister(static_cast<uint8_t>(kMaxProgramRegisters - 1));
        EXPECT_TRUE(builder.AcquireRegister().has_value());
    }

    TEST(ProgramInterpreterTest, RegistersAreAStack) {
        ProgramBuilder builder;
        const uint8_t a = builder.AcquireRegister().value();
        const uint8_t b = builder.AcquireRegister().value();
        EXPECT_EQ(a, 0);
        EXPECT_EQ(b, 1);
        builder.ReleaseRegister(b);
        EXPECT_EQ(builder.AcquireRegister().value(), 1);
    }

    TEST(ProgramInterpreterTest, FinishRefusesWhatCannotRun) {
        {
            ProgramBuilder builder;
            EXPECT_FALSE(builder.Finish(ProgramType::Int32, 0).has_value()) << "no instruction";
        }
        {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.Emit(Opcode::LoadConst, reg, 0, 0, 0, 3);
            const auto program = builder.Finish(ProgramType::Int32, reg);
            ASSERT_FALSE(program.has_value());
            EXPECT_NE(program.error().message.find("constant 3"), String::npos) << program.error().message;
        }
        {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadConst(reg, 1);
            builder.Emit(Opcode::Invalid, reg);
            EXPECT_FALSE(builder.Finish(ProgramType::Int32, reg).has_value());
        }
        {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadConst(reg, 1);
            builder.Emit(Opcode::AddInt32, 7, reg, reg);
            EXPECT_FALSE(builder.Finish(ProgramType::Int32, reg).has_value()) << "writes a register never acquired";
        }
        {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.Emit(Opcode::LoadSlotInt32, reg, static_cast<uint8_t>(kMaxContextLevels));
            EXPECT_FALSE(builder.Finish(ProgramType::Int32, reg).has_value()) << "a level beyond the context";
        }
    }

    TEST(ProgramInterpreterTest, EqualConstantsAreOneEntry) {
        ProgramBuilder builder;
        EXPECT_EQ(builder.AddConstant(7), builder.AddConstant(7));
        EXPECT_NE(builder.AddConstant(7), builder.AddConstant(8));
    }

    TEST(ProgramInterpreterTest, ThePoolGivesOneIdToEqualPrograms) {
        const ProgramId first = ProgramPool::Intern(ConstantProgram(1001)).value();
        const ProgramId again = ProgramPool::Intern(ConstantProgram(1001)).value();
        const ProgramId other = ProgramPool::Intern(ConstantProgram(1002)).value();
        EXPECT_EQ(first, again);
        EXPECT_NE(first, other);
        EXPECT_EQ(Run<int32_t>(ProgramPool::Get(first), Context{}), 1001);
        EXPECT_EQ(Run<int32_t>(ProgramPool::Get(other), Context{}), 1002);
    }

    TEST(ProgramInterpreterTest, TheSourceTextIsNotPartOfTheIdentity) {
        ProgramBuilder builder;
        const uint8_t reg = builder.AcquireRegister().value();
        builder.EmitLoadConst(reg, 424242);
        const ProgramId first = ProgramPool::Intern(builder.Finish(ProgramType::Int32, reg, PoolString::Intern("424242")).value()).value();
        const ProgramId second = ProgramPool::Intern(builder.Finish(ProgramType::Int32, reg, PoolString::Intern("424241 + 1")).value()).value();
        EXPECT_EQ(first, second);
        EXPECT_EQ(ProgramPool::Get(first).Source(), PoolString::Intern("424242")) << "the first text stays";
    }

    TEST(ProgramInterpreterTest, TheAddressOfAProgramNeverMoves) {
        const ProgramId id = ProgramPool::Intern(ConstantProgram(2001)).value();
        const Program* address = &ProgramPool::Get(id);
        constexpr int kMore = 600;  // more than two chunks of the pool
        for (int i = 0; i < kMore; ++i) {
            ASSERT_TRUE(ProgramPool::Intern(ConstantProgram(3000 + i)).has_value());
        }
        EXPECT_EQ(address, &ProgramPool::Get(id));
        EXPECT_EQ(Run<int32_t>(*address, Context{}), 2001);
        EXPECT_GE(ProgramPool::Count(), static_cast<size_t>(kMore));
    }

    TEST(ProgramInterpreterTest, ThreadsInterningTheSameProgramsAgreeOnTheIds) {
        Array<Vector<ProgramId>, kThreadCount> ids;
        Vector<Thread> threads;
        for (int t = 0; t < kThreadCount; ++t) {
            threads.emplace_back("ProgramPoolTest", [&ids, t] {
                for (int i = 0; i < kProgramsPerThread; ++i) {
                    ids[static_cast<size_t>(t)].push_back(ProgramPool::Intern(ConstantProgram(10000 + i)).value());
                }
            });
        }
        for (Thread& thread : threads) {
            thread.join();
        }
        for (int t = 1; t < kThreadCount; ++t) {
            EXPECT_EQ(ids[static_cast<size_t>(t)], ids[0]);
        }
        for (int i = 0; i < kProgramsPerThread; ++i) {
            EXPECT_EQ(Run<int32_t>(ProgramPool::Get(ids[0][static_cast<size_t>(i)]), Context{}), 10000 + i);
        }
    }

    TEST(ProgramInterpreterTest, TheInstructionIsTwelveBytes) {
        EXPECT_EQ(sizeof(Instruction), 12U);
        EXPECT_EQ(sizeof(RegisterImage), 16U);
        EXPECT_EQ(alignof(RegisterImage), 16U);
    }

    TEST(ProgramInterpreterTest, DisassemblyNamesTheInstructions) {
        const String text = ConstantProgram(5).Disassemble();
        EXPECT_NE(text.find("LoadConst"), String::npos) << text;
        EXPECT_NE(text.find("Int32"), String::npos) << text;
    }

}  // namespace BECore::Tests
