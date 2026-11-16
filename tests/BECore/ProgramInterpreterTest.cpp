// The instruction set and the interpreter of programs (Values/Programs/): every row of BE_PROGRAM_ROWS and BE_PROGRAM_CONVERSIONS against a plain
// C++ reference over edge inputs, the rules of the operations by number, the program pool and the builder. The programs are assembled by hand.

#include <BECore/Reflection/TypeDesc.h>
#include <BECore/Threading/Thread.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Programs/Components.h>
#include <BECore/Values/Programs/Interpreter.h>
#include <BECore/Values/Programs/Kernels.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <Common/Printers.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
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

        template <>
        Vector<Vec2> Edges<Vec2>() {
            return {Vec2{0.0f, 0.0f}, Vec2{1.0f, 2.0f}, Vec2{1.0f, 3.0f}, Vec2{2.0f, 2.0f}, Vec2{kNaN, 0.0f}, Vec2{0.0f, kNaN}, Vec2{kInf, -kInf}, Vec2{-0.0f, 0.0f}};
        }

        template <>
        Vector<Vec3> Edges<Vec3>() {
            return {Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 2.0f, 3.0f}, Vec3{1.0f, 2.0f, 4.0f},  Vec3{1.0f, 3.0f, 3.0f}, Vec3{2.0f, 2.0f, 3.0f},
                    Vec3{kNaN, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, kNaN}, Vec3{kInf, -kInf, 0.0f}, Vec3{-0.0f, 0.0f, 0.0f}};
        }

        template <>
        Vector<Vec4> Edges<Vec4>() {
            return {Vec4{0.0f, 0.0f, 0.0f, 0.0f}, Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{1.0f, 2.0f, 3.0f, 5.0f},  Vec4{1.0f, 2.0f, 4.0f, 4.0f},
                    Vec4{kNaN, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, kNaN}, Vec4{kInf, -kInf, 0.0f, 1.0f}, Vec4{0.0f, -0.0f, 0.0f, 0.0f}};
        }

        template <>
        Vector<IVec2> Edges<IVec2>() {
            return {IVec2{0, 0}, IVec2{1, 2}, IVec2{1, 3}, IVec2{2, 2}, IVec2{kInt32Min, kInt32Max}, IVec2{kInt32Max, kInt32Min}, IVec2{-1, -1}};
        }

        template <>
        Vector<Quat> Edges<Quat>() {
            return {Quat{0.0f, 0.0f, 0.0f, 1.0f}, Quat{0.0f, 0.0f, 0.0f, 0.0f},  Quat{0.0f, 0.0f, 0.70710678f, 0.70710678f}, Quat{0.0f, 0.0f, 0.70710678f, -0.70710678f}, Quat{1.0f, 0.0f, 0.0f, 0.0f},
                    Quat{0.0f, 0.0f, 0.0f, kNaN}, Quat{0.0f, -0.0f, 0.0f, 1.0f}, Quat{0.0f, 0.0f, 0.01f, 0.99995f}};
        }

        template <>
        Vector<Color> Edges<Color>() {
            return {Color{0, 0, 0, 0}, Color{255, 255, 255, 255}, Color{10, 20, 30, 255}, Color{10, 20, 30, 254}, Color{10, 20, 31, 255}, Color{11, 20, 30, 255}, Color{10, 21, 30, 255}};
        }

        template <>
        Vector<ProgramEnum> Edges<ProgramEnum>() {
            return {ProgramEnum{0}, ProgramEnum{1}, ProgramEnum{2}, ProgramEnum{3}, ProgramEnum{std::numeric_limits<uint32_t>::max()}};
        }

        template <>
        Vector<PoolString> Edges<PoolString>() {
            return {PoolString{},
                    PoolString::Intern(""),
                    PoolString::Intern("Fire"),
                    PoolString::Intern("fire"),
                    PoolString::Intern("Fire "),
                    PoolString::Intern("Water"),
                    PoolString::Intern("a longer text that is not a name of anything")};
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

        /// The vectors of floats: their members are `sizeof(T) / sizeof(float)` floats one after the other
        template <typename T>
        constexpr bool kIsFloatVector = std::is_same_v<T, Vec2> || std::is_same_v<T, Vec3> || std::is_same_v<T, Vec4> || std::is_same_v<T, Quat>;

        /// Floats are the same when equal or both NaN; a vector of floats when each member is, bit for bit apart from the NaNs (`-0` is not `0`); every other type moves as bytes (none has padding),
        /// which is what `Select` and a constant do
        template <typename T>
        bool Same(T a, T b) {
            if constexpr (std::floating_point<T>) {
                return (a != a && b != b) || a == b;
            } else if constexpr (std::is_arithmetic_v<T>) {
                return a == b;
            } else if constexpr (kIsFloatVector<T>) {
                constexpr size_t kLanes = sizeof(T) / sizeof(float);
                Array<float, kLanes> left{};
                Array<float, kLanes> right{};
                std::memcpy(left.data(), &a, sizeof(T));
                std::memcpy(right.data(), &b, sizeof(T));
                for (size_t i = 0; i < kLanes; ++i) {
                    const bool bothNan = left[i] != left[i] && right[i] != right[i];
                    if (!bothNan && std::memcmp(&left[i], &right[i], sizeof(float)) != 0) {
                        return false;
                    }
                }
                return true;
            } else {
                return std::memcmp(&a, &b, sizeof(T)) == 0;
            }
        }

        /// Equality of the types a program compares, spelled member by member (`Color` has no `operator==`)
        template <typename T>
        bool ReferenceEqual(const T& a, const T& b) {
            return a == b;
        }

        bool ReferenceEqual(const Vec2& a, const Vec2& b) {
            return a.x == b.x && a.y == b.y;
        }

        bool ReferenceEqual(const Vec3& a, const Vec3& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        bool ReferenceEqual(const Vec4& a, const Vec4& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        bool ReferenceEqual(const IVec2& a, const IVec2& b) {
            return a.x == b.x && a.y == b.y;
        }

        bool ReferenceEqual(const Quat& a, const Quat& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        bool ReferenceEqual(const Color& a, const Color& b) {
            return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
        }

        bool ReferenceEqual(const ProgramEnum& a, const ProgramEnum& b) {
            return a.index == b.index;
        }

        bool ReferenceEqual(const PoolString& a, const PoolString& b) {
            return a.ToStringView() == b.ToStringView();
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

        bool IsNanValue(float value) {
            return value != value;
        }

        /// `Lerp(Color)` of the engine for a finite @p t; the engine's cast of a NaN channel is undefined, so a NaN or infinite @p t is spelled out here with the channel 0 for a NaN
        Color ReferenceColorLerp(const Color& from, const Color& to, float t) {
            if (std::isfinite(t)) {
                return Lerp(from, to, t);
            }
            const auto channel = [t](unsigned char a, unsigned char b) {
                const float value = std::round(std::lerp(static_cast<float>(a), static_cast<float>(b), t));
                return IsNanValue(value) ? static_cast<unsigned char>(0) : static_cast<unsigned char>(std::clamp(value, 0.0f, 255.0f));
            };
            return {channel(from.r, to.r), channel(from.g, to.g), channel(from.b, to.b), channel(from.a, to.a)};
        }

        /// The operations of the math types and the rest that are not numbers, through the engine's own operators and functions; `IVec2` through 64-bit words
        template <typename Op, typename T, typename W>
        auto ReferenceOfObjects(T a, T b, W c) {
            if constexpr (std::is_same_v<Op, K::Eq>) {
                return ReferenceEqual(a, b);
            } else if constexpr (std::is_same_v<Op, K::Ne>) {
                return !ReferenceEqual(a, b);
            } else if constexpr (std::is_same_v<T, IVec2>) {
                if constexpr (std::is_same_v<Op, K::Add>) {
                    return IVec2{WrapTo<int32_t>(Word(a.x) + Word(b.x)), WrapTo<int32_t>(Word(a.y) + Word(b.y))};
                } else if constexpr (std::is_same_v<Op, K::Sub>) {
                    return IVec2{WrapTo<int32_t>(Word(a.x) - Word(b.x)), WrapTo<int32_t>(Word(a.y) - Word(b.y))};
                } else {
                    static_assert(std::is_same_v<Op, K::Neg>, "a row of IVec2 without a reference");
                    return IVec2{WrapTo<int32_t>(Word(int32_t{0}) - Word(a.x)), WrapTo<int32_t>(Word(int32_t{0}) - Word(a.y))};
                }
            } else if constexpr (std::is_same_v<Op, K::Add>) {
                return a + b;
            } else if constexpr (std::is_same_v<Op, K::Sub>) {
                return a - b;
            } else if constexpr (std::is_same_v<Op, K::Mul>) {
                return a * b;
            } else if constexpr (std::is_same_v<Op, K::Neg>) {
                return -a;
            } else if constexpr (std::is_same_v<Op, K::Dot>) {
                return Dot(a, b);
            } else if constexpr (std::is_same_v<Op, K::Length>) {
                return a.Length();
            } else if constexpr (std::is_same_v<Op, K::Normalize>) {
                return a.Normalized();
            } else {
                static_assert(std::is_same_v<Op, K::Lerp>, "a row without a reference");
                if constexpr (std::is_same_v<T, Quat>) {
                    return Slerp(a, b, c);
                } else if constexpr (std::is_same_v<T, Color>) {
                    return ReferenceColorLerp(a, b, c);
                } else {
                    return Lerp(a, b, c);
                }
            }
        }

        /// The plain C++ meaning of every operation, written another way than the kernel: integers through 64-bit words, the rules of the stage plan spelled out; @p c is the third operand, the weight
        /// of a `Lerp`
        template <typename Op, typename T, typename W = T>
        auto Reference(T a, T b = T{}, W c = W{}) {
            if constexpr (!std::is_arithmetic_v<T>) {
                return ReferenceOfObjects<Op, T, W>(a, b, c);
            } else if constexpr (std::is_same_v<Op, K::Add>) {
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
                return ReferenceEqual(a, b);
            } else if constexpr (std::is_same_v<Op, K::Ne>) {
                return !ReferenceEqual(a, b);
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

        /// The most operands a hand-assembled instruction loads: `Pack` takes four, in consecutive registers
        constexpr size_t kMaxOperandRegisters = 4;

        /// Runs one instruction over constants: the operands are loaded into the first registers, the result is read from the next one
        template <typename R, typename... Args>
        R RunOpcode(Opcode op, Args... args) {
            ProgramBuilder builder;
            static_assert(sizeof...(Args) <= kMaxOperandRegisters);
            Array<uint8_t, kMaxOperandRegisters> registers{};
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
            if constexpr (std::is_arithmetic_v<T>) {
                return Format("{}", value);
            } else {
                String text;
                Array<uint8_t, sizeof(T)> bytes{};
                std::memcpy(bytes.data(), &value, sizeof(T));
                for (const uint8_t byte : bytes) {
                    text += Format("{:02x}", byte);
                }
                return text;
            }
        }

        /// `Extract` of @p value: the component index is the immediate of the instruction
        template <typename R, typename T>
        R RunExtract(Opcode op, T value, uint32_t index) {
            ProgramBuilder builder;
            const uint8_t source = builder.AcquireRegister().value();
            builder.EmitLoadConst(source, value);
            const uint8_t dst = builder.AcquireRegister().value();
            builder.Emit(op, dst, source, 0, 0, index);
            const auto program = builder.Finish(kProgramTypeOf<R>, dst);
            EXPECT_TRUE(program.has_value());
            return program.has_value() ? Run<R>(*program, Context{}) : R{};
        }

        /// `Pack` of @p values, loaded into consecutive registers: the instruction names the first
        template <typename T, typename Value, size_t N>
        T RunPack(Opcode op, const Array<Value, N>& values) {
            ProgramBuilder builder;
            uint8_t first = 0;
            for (size_t i = 0; i < N; ++i) {
                const uint8_t reg = builder.AcquireRegister().value();
                builder.EmitLoadConst(reg, values[i]);
                first = i == 0 ? reg : first;
            }
            const uint8_t dst = builder.AcquireRegister().value();
            builder.Emit(op, dst, first);
            const auto program = builder.Finish(kProgramTypeOf<T>, dst);
            EXPECT_TRUE(program.has_value());
            return program.has_value() ? Run<T>(*program, Context{}) : T{};
        }

        /// The component of a value by its member, to compare with what `Extract` reads at an offset
        template <typename T>
        auto ReferenceComponent(const T& value, uint32_t index) {
            if constexpr (std::is_same_v<T, Color>) {
                return static_cast<int32_t>(index == 0 ? value.r : (index == 1 ? value.g : (index == 2 ? value.b : value.a)));
            } else if constexpr (std::is_same_v<T, Vec2> || std::is_same_v<T, IVec2>) {
                return index == 0 ? value.x : value.y;
            } else if constexpr (std::is_same_v<T, Vec3>) {
                return index == 0 ? value.x : (index == 1 ? value.y : value.z);
            } else {
                return index == 0 ? value.x : (index == 1 ? value.y : (index == 2 ? value.z : value.w));
            }
        }

        template <typename T, typename Value, size_t N>
        T ReferencePack(const Array<Value, N>& v) {
            if constexpr (std::is_same_v<T, Color>) {
                const auto channel = [](int32_t value) { return static_cast<unsigned char>(std::clamp(value, 0, 255)); };
                return Color{channel(v[0]), channel(v[1]), channel(v[2]), channel(v[3])};
            } else if constexpr (N == 2) {
                return T{v[0], v[1]};
            } else if constexpr (N == 3) {
                return T{v[0], v[1], v[2]};
            } else {
                return T{v[0], v[1], v[2], v[3]};
            }
        }

        int g_rowsChecked = 0;

        template <typename Op, typename T>
        void CheckRow(Opcode op) {
            ++g_rowsChecked;
            using Result = K::ResultOf<Op, T>;
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
            } else if constexpr (Op::kShape == K::OpShape::Extract) {
                for (const T a : Edges<T>()) {
                    for (uint32_t index = 0; index < ComponentTraits<T>::kCount; ++index) {
                        const Result got = RunExtract<Result>(op, a, index);
                        EXPECT_TRUE(Same<Result>(got, ReferenceComponent(a, index))) << name << "(" << Show(a) << ", " << index << ") gave " << Show(got);
                    }
                }
            } else if constexpr (Op::kShape == K::OpShape::Pack) {
                using Value = typename ComponentTraits<T>::Value;
                constexpr size_t kCount = ComponentTraits<T>::kCount;
                // Distinct operands, so a swapped pair shows; each position then takes every edge in turn
                Array<Value, kCount> base{};
                for (size_t i = 0; i < kCount; ++i) {
                    base[i] = static_cast<Value>(3 * (i + 1));
                }
                Vector<Array<Value, kCount>> cases{base};
                for (size_t position = 0; position < kCount; ++position) {
                    for (const Value edge : Edges<Value>()) {
                        Array<Value, kCount> operands = base;
                        operands[position] = edge;
                        cases.push_back(operands);
                    }
                }
                for (const auto& operands : cases) {
                    const T got = RunPack<T>(op, operands);
                    EXPECT_TRUE(Same<T>(got, ReferencePack<T>(operands))) << name << " gave " << Show(got);
                }
            } else if constexpr (Op::kShape == K::OpShape::Blend) {
                using Weight = typename Op::template Weight<T>;
                for (const T a : Edges<T>()) {
                    for (const T b : Edges<T>()) {
                        for (const Weight t : Edges<Weight>()) {
                            const T got = RunOpcode<T>(op, a, b, t);
                            EXPECT_TRUE(Same<T>(got, (Reference<Op, T, Weight>(a, b, t))))
                                << name << "(" << Show(a) << ", " << Show(b) << ", " << Show(t) << ") gave " << Show(got) << ", the reference " << Show((Reference<Op, T, Weight>(a, b, t)));
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

        /// The plain C++ meaning of a mixed pair; `IVec2` through 64-bit words
        template <typename Op, typename L, typename R>
        auto ReferenceMixed(L a, R b) {
            if constexpr (std::is_same_v<Op, K::Div>) {
                return a / b;
            } else if constexpr (std::is_same_v<L, Quat>) {
                return a.Rotate(b);
            } else if constexpr (std::is_same_v<L, IVec2>) {
                return IVec2{WrapTo<int32_t>(Word(a.x) * Word(b)), WrapTo<int32_t>(Word(a.y) * Word(b))};
            } else if constexpr (std::is_same_v<R, IVec2>) {
                return IVec2{WrapTo<int32_t>(Word(a) * Word(b.x)), WrapTo<int32_t>(Word(a) * Word(b.y))};
            } else {
                return a * b;
            }
        }

        template <typename Op, typename L, typename R>
        void CheckMixedRow(Opcode op) {
            ++g_rowsChecked;
            using Result = K::MixedResultOf<Op, L, R>;
            const String name(EnumUtils<Opcode>::ToString(op));
            for (const L a : Edges<L>()) {
                for (const R b : Edges<R>()) {
                    const Result got = RunOpcode<Result>(op, a, b);
                    const Result expected = ReferenceMixed<Op, L, R>(a, b);
                    EXPECT_TRUE(Same<Result>(got, expected)) << name << "(" << Show(a) << ", " << Show(b) << ") gave " << Show(got) << ", the reference " << Show(expected);
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
#define BE_CHECK_MIXED(Op, LName, LCpp, RName, RCpp) CheckMixedRow<Kernel::Op, LCpp, RCpp>(Opcode::Op##LName##RName);
        BE_PROGRAM_MIXED_ROWS(BE_CHECK_MIXED)
#undef BE_CHECK_MIXED
#define BE_CHECK_CONVERSION(Src, SrcCpp, Dst, DstCpp) CheckConversion<SrcCpp, DstCpp>(Opcode::Src##To##Dst);
        BE_PROGRAM_CONVERSIONS(BE_CHECK_CONVERSION)
#undef BE_CHECK_CONVERSION
        // Every opcode but the three untyped ones is a row or a conversion: a row that the matrix skipped would leave the count short
        EXPECT_EQ(static_cast<size_t>(g_rowsChecked) + 3, EnumUtils<Opcode>::Count());
    }

    TEST(ProgramInterpreterTest, TheVectorsOfIntegersWrapLikeIntegers) {
        const IVec2 high{kInt32Max, kInt32Min};
        EXPECT_EQ(Eval<IVec2>(Opcode::AddIVec2, high, IVec2{1, -1}), (IVec2{kInt32Min, kInt32Max}));
        EXPECT_EQ(Eval<IVec2>(Opcode::SubIVec2, IVec2{kInt32Min, 0}, IVec2{1, 1}), (IVec2{kInt32Max, -1}));
        EXPECT_EQ(Eval<IVec2>(Opcode::NegIVec2, IVec2{kInt32Min, 5}), (IVec2{kInt32Min, -5}));
        EXPECT_EQ(Eval<IVec2>(Opcode::MulIVec2Int32, IVec2{65536, kInt32Min}, 65536), (IVec2{0, 0}));
        EXPECT_EQ(Eval<IVec2>(Opcode::MulIVec2Int32, IVec2{3, -4}, -2), (IVec2{-6, 8}));
        EXPECT_EQ(Eval<IVec2>(Opcode::MulInt32IVec2, kInt32Max, IVec2{2, 3}), (IVec2{-2, kInt32Max - 2}));
    }

    TEST(ProgramInterpreterTest, AQuarterTurnAboutZRotatesXToY) {
        const Quat quarter = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f);
        const Vec3 rotated = Eval<Vec3>(Opcode::MulQuatVec3, quarter, Vec3{1.0f, 0.0f, 0.0f});
        EXPECT_NEAR(rotated.x, 0.0f, 1e-6f);
        EXPECT_NEAR(rotated.y, 1.0f, 1e-6f);
        EXPECT_NEAR(rotated.z, 0.0f, 1e-6f);
        const Vec3 same = quarter.Rotate(Vec3{1.0f, 0.0f, 0.0f});
        EXPECT_TRUE(Same<Vec3>(rotated, same)) << "Quat * Vec3 is Quat::Rotate";
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::MulQuatVec3, Quat{}, Vec3{4.0f, 5.0f, 6.0f}), (Vec3{4.0f, 5.0f, 6.0f}))) << "the identity turns nothing";
        // A non-unit quaternion is no rotation, but it is still a value and never a trap
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::MulQuatVec3, Quat{}, Vec3{kNaN, kInf, 1.0f}), (Quat{}.Rotate(Vec3{kNaN, kInf, 1.0f}))));
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::MulQuatVec3, Quat{0.0f, 0.0f, 0.0f, 0.0f}, Vec3{1.0f, 2.0f, 3.0f}), (Vec3{1.0f, 2.0f, 3.0f}))) << "a zero quaternion rotates by nothing";
    }

    TEST(ProgramInterpreterTest, QuatProductIsHamiltonAndAppliesTheRightOperandFirst) {
        const Quat quarterZ = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f);
        const Quat quarterX = Quat::FromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, std::numbers::pi_v<float> / 2.0f);
        const Quat product = Eval<Quat>(Opcode::MulQuat, quarterZ, quarterX);
        EXPECT_TRUE(Same<Quat>(product, quarterZ * quarterX));
        const Vec3 viaProduct = product.Rotate(Vec3{0.0f, 1.0f, 0.0f});
        const Vec3 oneByOne = quarterZ.Rotate(quarterX.Rotate(Vec3{0.0f, 1.0f, 0.0f}));
        EXPECT_NEAR(viaProduct.x, oneByOne.x, 1e-6f);
        EXPECT_NEAR(viaProduct.y, oneByOne.y, 1e-6f);
        EXPECT_NEAR(viaProduct.z, oneByOne.z, 1e-6f);
    }

    TEST(ProgramInterpreterTest, LengthDotAndNormalizeOfVectors) {
        EXPECT_EQ(Eval<float>(Opcode::LengthVec3, Vec3{2.0f, 3.0f, 6.0f}), 7.0f);
        EXPECT_EQ(Eval<float>(Opcode::LengthVec4, Vec4{1.0f, 2.0f, 2.0f, 4.0f}), 5.0f);
        EXPECT_EQ(Eval<float>(Opcode::DotVec3, Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, -5.0f, 6.0f}), 12.0f);
        EXPECT_EQ(Eval<float>(Opcode::DotVec4, Vec4{1.0f, 0.0f, 0.0f, 1.0f}, Vec4{2.0f, 7.0f, 7.0f, 3.0f}), 5.0f);
        EXPECT_EQ(Eval<float>(Opcode::DotQuat, Quat{}, Quat{0.0f, 0.0f, 0.0f, -1.0f}), -1.0f);
        EXPECT_TRUE(std::isnan(Eval<float>(Opcode::LengthVec3, Vec3{kNaN, 0.0f, 0.0f})));
        EXPECT_TRUE(std::isinf(Eval<float>(Opcode::LengthVec3, Vec3{kInf, 0.0f, 0.0f})));
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::NormalizeVec3, Vec3{0.0f, 3.0f, 4.0f}), (Vec3{0.0f, 0.6f, 0.8f})));
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::NormalizeVec3, Vec3{}), Vec3{})) << "a zero vector normalizes to zero";
        EXPECT_TRUE(Same<Vec4>(Eval<Vec4>(Opcode::NormalizeVec4, Vec4{}), Vec4{}));
        EXPECT_TRUE(Same<Quat>(Eval<Quat>(Opcode::NormalizeQuat, Quat{0.0f, 0.0f, 0.0f, 0.0f}), Quat{})) << "the engine's Normalized of a zero quaternion is the identity";
        EXPECT_TRUE(Same<Quat>(Eval<Quat>(Opcode::NormalizeQuat, Quat{0.0f, 0.0f, 0.0f, 2.0f}), Quat{}));
    }

    TEST(ProgramInterpreterTest, LerpOfTheVectorsIsPerComponentAndQuatTakesTheShortestArc) {
        EXPECT_TRUE(Same<Vec3>(Eval<Vec3>(Opcode::LerpVec3, Vec3{0.0f, 2.0f, -4.0f}, Vec3{4.0f, 2.0f, 4.0f}, 0.25f), (Vec3{1.0f, 2.0f, -2.0f})));
        EXPECT_TRUE(Same<Vec2>(Eval<Vec2>(Opcode::LerpVec2, Vec2{0.0f, 0.0f}, Vec2{2.0f, 4.0f}, 2.0f), (Vec2{4.0f, 8.0f}))) << "t is not clamped";
        EXPECT_TRUE(Same<Vec4>(Eval<Vec4>(Opcode::LerpVec4, Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{5.0f, 6.0f, 7.0f, 8.0f}, 0.0f), (Vec4{1.0f, 2.0f, 3.0f, 4.0f})));
        const Quat identity;
        const Quat quarter = Quat::FromAxisAngle(Vec3{0.0f, 0.0f, 1.0f}, std::numbers::pi_v<float> / 2.0f);
        EXPECT_TRUE(Same<Quat>(Eval<Quat>(Opcode::LerpQuat, identity, quarter, 0.5f), Slerp(identity, quarter, 0.5f)));
        const Quat eighth = Eval<Quat>(Opcode::LerpQuat, identity, quarter, 0.5f);
        EXPECT_NEAR(eighth.z, std::sin(std::numbers::pi_v<float> / 8.0f), 1e-6f) << "half way is half the angle: a spherical, not a linear, interpolation";
        const Quat negated{-quarter.x, -quarter.y, -quarter.z, -quarter.w};
        EXPECT_TRUE(Same<Quat>(Eval<Quat>(Opcode::LerpQuat, identity, negated, 0.5f), Slerp(identity, negated, 0.5f))) << "q and -q are one rotation: the short way round";
    }

    TEST(ProgramInterpreterTest, LerpOfAColorRoundsClampsAndNeverCastsANan) {
        const Color from{0, 100, 255, 10};
        const Color to{255, 200, 0, 11};
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, from, to, 0.0f), from));
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, from, to, 1.0f), to));
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, from, to, 0.5f), Lerp(from, to, 0.5f)));
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, from, to, 4.0f), (Color{255, 255, 0, 14}))) << "an overshooting t clamps to the byte";
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, from, to, -4.0f), (Color{0, 0, 255, 6})));
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, Color{}, Color{}, kInf), Color{})) << "0 * infinity is NaN, which is the channel 0 and not a cast of a NaN";
        EXPECT_TRUE(Same<Color>(Eval<Color>(Opcode::LerpColor, Color{}, Color{}, kNaN), Color{}));
    }

    TEST(ProgramInterpreterTest, TheComponentTableNamesTheComponentsOfTheSixTypes) {
        const auto names = [](ProgramType type) {
            String text;
            const ComponentSet set = ComponentsOf(type);
            for (uint32_t i = 0; i < set.count; ++i) {
                text += String(set.names[i]);
            }
            return text;
        };
        EXPECT_EQ(names(ProgramType::Vec2), "xy");
        EXPECT_EQ(names(ProgramType::Vec3), "xyz");
        EXPECT_EQ(names(ProgramType::Vec4), "xyzw");
        EXPECT_EQ(names(ProgramType::IVec2), "xy");
        EXPECT_EQ(names(ProgramType::Quat), "xyzw");
        EXPECT_EQ(names(ProgramType::Color), "rgba");
        EXPECT_EQ(ComponentsOf(ProgramType::Vec3).type, ProgramType::Float);
        EXPECT_EQ(ComponentsOf(ProgramType::Quat).type, ProgramType::Float);
        EXPECT_EQ(ComponentsOf(ProgramType::IVec2).type, ProgramType::Int32);
        EXPECT_EQ(ComponentsOf(ProgramType::Color).type, ProgramType::Int32) << "a channel is an int to a program";
        EXPECT_EQ(ComponentsOf(ProgramType::Color).Offset(3), 3U) << "a channel is a byte in the value";
        EXPECT_EQ(ComponentsOf(ProgramType::Vec4).Offset(3), 3 * sizeof(float));
        EXPECT_EQ(ComponentsOf(ProgramType::Vec3).Find("z"), 2U);
        EXPECT_FALSE(ComponentsOf(ProgramType::Vec3).Find("w").has_value());
        EXPECT_FALSE(ComponentsOf(ProgramType::Vec3).Find("").has_value()) << "the unused slots of the table are not names";
        EXPECT_FALSE(ComponentsOf(ProgramType::Color).Find("x").has_value());
        for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
            const bool hasComponents =
                type == ProgramType::Vec2 || type == ProgramType::Vec3 || type == ProgramType::Vec4 || type == ProgramType::IVec2 || type == ProgramType::Quat || type == ProgramType::Color;
            const String name(EnumUtils<ProgramType>::ToString(type));
            EXPECT_EQ(ComponentsOf(type).count > 0, hasComponents) << name;
            EXPECT_EQ(OpcodeOf(ProgramOperation::Extract, type) != Opcode::Invalid, hasComponents) << name;
            EXPECT_EQ(OpcodeOf(ProgramOperation::Pack, type) != Opcode::Invalid, hasComponents) << name;
            if (hasComponents) {
                EXPECT_EQ(ComponentOpcodeType(OpcodeOf(ProgramOperation::Extract, type)), type) << name;
                EXPECT_EQ(ComponentOpcodeType(OpcodeOf(ProgramOperation::Pack, type)), type) << name;
                EXPECT_TRUE(IsPack(OpcodeOf(ProgramOperation::Pack, type))) << name;
                EXPECT_FALSE(IsPack(OpcodeOf(ProgramOperation::Extract, type))) << name;
            }
        }
        EXPECT_FALSE(ComponentOpcodeType(Opcode::AddInt32).has_value());
    }

    TEST(ProgramInterpreterTest, ExtractReadsTheComponentsOfALoadedField) {
        ValueWorld world;
        world.caster.position = Vec2{-1.5f, 8.25f};
        world.caster.cell = IVec2{-7, 40000};
        world.caster.velocity = Vec3{0.5f, -2.0f, 9.0f};
        world.caster.plane = Vec4{1.0f, 2.0f, 3.0f, -4.0f};
        world.caster.facing = Quat{0.1f, 0.2f, 0.3f, 0.4f};
        world.caster.tint = Color{1, 2, 3, 250};

        const auto extract = [&](ProgramType type, StringView path, const TypeDesc& desc, uint32_t index) {
            const ComponentSet set = ComponentsOf(type);
            ProgramBuilder builder;
            const uint8_t value = builder.AcquireRegister().value();
            builder.EmitLoadSlot(value, type, Link(world, path, desc));
            const uint8_t dst = builder.AcquireRegister().value();
            builder.Emit(OpcodeOf(ProgramOperation::Extract, type), dst, value, 0, 0, index);
            return builder.Finish(set.type, dst).value();
        };
        EXPECT_EQ(Run<float>(extract(ProgramType::Vec2, "caster.position", DescribeType<Vec2>(), 1), world.context), 8.25f);
        EXPECT_EQ(Run<int32_t>(extract(ProgramType::IVec2, "caster.cell", DescribeType<IVec2>(), 0), world.context), -7);
        EXPECT_EQ(Run<int32_t>(extract(ProgramType::IVec2, "caster.cell", DescribeType<IVec2>(), 1), world.context), 40000);
        EXPECT_EQ(Run<float>(extract(ProgramType::Vec3, "caster.velocity", DescribeType<Vec3>(), 2), world.context), 9.0f);
        EXPECT_EQ(Run<float>(extract(ProgramType::Vec4, "caster.plane", DescribeType<Vec4>(), 3), world.context), -4.0f);
        EXPECT_EQ(Run<float>(extract(ProgramType::Quat, "caster.facing", DescribeType<Quat>(), 2), world.context), 0.3f);
        EXPECT_EQ(Run<int32_t>(extract(ProgramType::Color, "caster.tint", DescribeType<Color>(), 3), world.context), 250);
        EXPECT_EQ(Run<int32_t>(extract(ProgramType::Color, "caster.tint", DescribeType<Color>(), 0), world.context), 1);
    }

    TEST(ProgramInterpreterTest, PackThenExtractRoundTripsAndPackDoesNotNormalize) {
        const Quat packed = RunPack<Quat>(Opcode::PackQuat, Array<float, 4>{3.0f, 4.0f, 0.0f, 12.0f});
        EXPECT_TRUE(Same<Quat>(packed, (Quat{3.0f, 4.0f, 0.0f, 12.0f}))) << "a quaternion is packed as given, even one that is no rotation";
        for (uint32_t index = 0; index < 4; ++index) {
            EXPECT_EQ(RunExtract<float>(Opcode::ExtractQuat, packed, index), ReferenceComponent(packed, index));
        }
        const Vec3 vec3 = RunPack<Vec3>(Opcode::PackVec3, Array<float, 3>{-1.5f, 0.0f, kInf});
        const Vec3 expected{-1.5f, 0.0f, kInf};
        EXPECT_EQ(std::memcmp(&vec3, &expected, sizeof(expected)), 0) << "the bytes of the C++ value";
        const IVec2 cell = RunPack<IVec2>(Opcode::PackIVec2, Array<int32_t, 2>{kInt32Min, kInt32Max});
        EXPECT_EQ(RunExtract<int32_t>(Opcode::ExtractIVec2, cell, 0), kInt32Min);
        EXPECT_EQ(RunExtract<int32_t>(Opcode::ExtractIVec2, cell, 1), kInt32Max);
    }

    TEST(ProgramInterpreterTest, PackColorClampsEachChannelToAByte) {
        const Color clamped = RunPack<Color>(Opcode::PackColor, Array<int32_t, 4>{-1, 0, 255, 256});
        EXPECT_TRUE(Same<Color>(clamped, (Color{0, 0, 255, 255})));
        EXPECT_TRUE(Same<Color>(RunPack<Color>(Opcode::PackColor, Array<int32_t, 4>{kInt32Min, kInt32Max, 1, 254}), (Color{0, 255, 1, 254})));
    }

    TEST(ProgramInterpreterTest, FinishRefusesAComponentThatIsNotThere) {
        ProgramBuilder extract;
        const uint8_t value = extract.AcquireRegister().value();
        extract.EmitLoadConst(value, Vec3{});
        const uint8_t dst = extract.AcquireRegister().value();
        extract.Emit(Opcode::ExtractVec3, dst, value, 0, 0, 3);
        const auto refusedExtract = extract.Finish(ProgramType::Float, dst);
        ASSERT_FALSE(refusedExtract.has_value());
        EXPECT_NE(refusedExtract.error().message.find("component 3"), String::npos) << refusedExtract.error().message;

        ProgramBuilder pack;
        const uint8_t first = pack.AcquireRegister().value();
        pack.EmitLoadConst(first, 1.0f);
        const uint8_t second = pack.AcquireRegister().value();
        pack.EmitLoadConst(second, 2.0f);
        const uint8_t out = pack.AcquireRegister().value();
        pack.Emit(Opcode::PackVec3, out, first);
        const auto refusedPack = pack.Finish(ProgramType::Vec3, out);
        EXPECT_TRUE(refusedPack.has_value()) << "three registers are acquired: the operands are r0, r1 and r2";
        ProgramBuilder tooFar;
        const uint8_t only = tooFar.AcquireRegister().value();
        tooFar.EmitLoadConst(only, 1.0f);
        tooFar.Emit(Opcode::PackVec2, only, only);
        const auto refusedTooFar = tooFar.Finish(ProgramType::Vec2, only);
        ASSERT_FALSE(refusedTooFar.has_value());
        EXPECT_NE(refusedTooFar.error().message.find("packs 2 operands"), String::npos) << refusedTooFar.error().message;
    }

    TEST(ProgramInterpreterTest, AnOperationATypeLacksHasNoRowAndMixedPairsAreListed) {
        EXPECT_EQ(OpcodeOf(ProgramOperation::Add, ProgramType::Quat), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Add, ProgramType::Color), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Sub, ProgramType::Quat), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lt, ProgramType::Vec3), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lerp, ProgramType::IVec2), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lerp, ProgramType::Int32), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Neg, ProgramType::Vec2), Opcode::Invalid) << "the engine's Vec2 has no unary minus";
        EXPECT_EQ(OpcodeOf(ProgramOperation::Mul, ProgramType::Vec3), Opcode::Invalid) << "a vector times a vector is neither a dot nor a cross";
        EXPECT_EQ(OpcodeOf(ProgramOperation::Div, ProgramType::Vec3), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Mod, ProgramType::IVec2), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Abs, ProgramType::Vec3), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Dot, ProgramType::Vec2), Opcode::Invalid) << "the engine has no Dot of Vec2";
        EXPECT_EQ(OpcodeOf(ProgramOperation::Dot, ProgramType::Float), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Length, ProgramType::Quat), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Normalize, ProgramType::Color), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Mul, ProgramType::Quat), Opcode::MulQuat);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lerp, ProgramType::Quat), Opcode::LerpQuat);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Neg, ProgramType::IVec2), Opcode::NegIVec2);

        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Vec3, ProgramType::Float), Opcode::MulVec3Float);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Float, ProgramType::Vec3), Opcode::MulFloatVec3);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Div, ProgramType::Vec3, ProgramType::Float), Opcode::DivVec3Float);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::IVec2, ProgramType::Int32), Opcode::MulIVec2Int32);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Int32, ProgramType::IVec2), Opcode::MulInt32IVec2);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Quat, ProgramType::Vec3), Opcode::MulQuatVec3);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Vec3, ProgramType::Quat), Opcode::Invalid) << "the rotation is on the left";
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Vec3, ProgramType::Int32), Opcode::Invalid) << "a vector takes exactly its scalar";
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::IVec2, ProgramType::Float), Opcode::Invalid);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Div, ProgramType::IVec2, ProgramType::Int32), Opcode::Invalid);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Div, ProgramType::Float, ProgramType::Vec3), Opcode::Invalid);
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Mul, ProgramType::Float, ProgramType::Double), Opcode::Invalid) << "two numbers never mix";
        EXPECT_EQ(MixedOpcodeOf(ProgramOperation::Add, ProgramType::Vec3, ProgramType::Float), Opcode::Invalid);
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

    TEST(ProgramInterpreterTest, SlotLoadsReadTheMathTypesTextAndEnums) {
        ValueWorld world;
        world.caster.position = Vec2{-1.5f, 8.25f};
        world.caster.cell = IVec2{-7, 40000};
        world.caster.velocity = Vec3{0.5f, -2.0f, 9.0f};
        world.caster.plane = Vec4{1.0f, 2.0f, 3.0f, -4.0f};
        world.caster.facing = Quat{0.0f, 0.70710678f, 0.0f, 0.70710678f};
        world.caster.tint = Color{1, 2, 3, 250};
        world.caster.title = PoolString::Intern("Scorch");
        world.caster.element = ValueElement::Earth;

        const auto load = [&](ProgramType type, StringView path, const TypeDesc& desc, auto expected) {
            using T = decltype(expected);
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadSlot(reg, type, Link(world, path, desc), static_cast<uint8_t>(desc.enumBytes));
            const Program program = builder.Finish(type, reg).value();
            EXPECT_TRUE(Same<T>(Run<T>(program, world.context), expected)) << String(path).c_str();
        };
        load(ProgramType::Vec2, "caster.position", DescribeType<Vec2>(), Vec2{-1.5f, 8.25f});
        load(ProgramType::IVec2, "caster.cell", DescribeType<IVec2>(), IVec2{-7, 40000});
        load(ProgramType::Vec3, "caster.velocity", DescribeType<Vec3>(), Vec3{0.5f, -2.0f, 9.0f});
        load(ProgramType::Vec4, "caster.plane", DescribeType<Vec4>(), Vec4{1.0f, 2.0f, 3.0f, -4.0f});
        load(ProgramType::Quat, "caster.facing", DescribeType<Quat>(), Quat{0.0f, 0.70710678f, 0.0f, 0.70710678f});
        load(ProgramType::Color, "caster.tint", DescribeType<Color>(), Color{1, 2, 3, 250});
        load(ProgramType::String, "caster.title", DescribeType<PoolString>(), PoolString::Intern("Scorch"));
        load(ProgramType::Enum, "caster.element", DescribeType<ValueElement>(), ValueElement::Earth);
        load(ProgramType::Vec2, "caster.span.offset", DescribeType<Vec2>(), world.caster.span.offset);
        load(ProgramType::Vec3, "cast.target", DescribeType<Vec3>(), Vec3{7.0f, 8.0f, 9.0f});
    }

    TEST(ProgramInterpreterTest, AnEnumFieldOfEveryWidthLoadsItsIndex) {
        ContextDecl declaration;
        static_cast<void>(declaration.Add<ValueEnumWidths>("widths"));
        ValueEnumWidths widths;
        Context context(declaration);
        context.Set(0, widths);

        const auto load = [&](StringView path, const TypeDesc& desc, auto expected) {
            using E = decltype(expected);
            const auto slot = LinkSlot(declaration, path, desc);
            ASSERT_TRUE(slot.has_value()) << String(path).c_str();
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadSlot(reg, ProgramType::Enum, *slot, static_cast<uint8_t>(desc.enumBytes));
            const Program program = builder.Finish(ProgramType::Enum, reg).value();
            EXPECT_EQ(Run<E>(program, context), expected) << String(path).c_str();
            EXPECT_EQ(Run<ProgramEnum>(program, context).index, static_cast<uint32_t>(expected)) << "the index is the value of a CORE_ENUM";
        };
        for (const ValueNarrowEnum value : EnumUtils<ValueNarrowEnum>::Values()) {
            widths.narrow = value;
            load("widths.narrow", DescribeType<ValueNarrowEnum>(), value);
        }
        for (const ValueShortEnum value : EnumUtils<ValueShortEnum>::Values()) {
            widths.wide16 = value;
            load("widths.wide16", DescribeType<ValueShortEnum>(), value);
        }
        for (const ValueWideEnum value : EnumUtils<ValueWideEnum>::Values()) {
            widths.wide32 = value;
            load("widths.wide32", DescribeType<ValueWideEnum>(), value);
        }
        EXPECT_EQ(DescribeType<ValueNarrowEnum>().enumBytes, 1U);
        EXPECT_EQ(DescribeType<ValueShortEnum>().enumBytes, 2U);
        EXPECT_EQ(DescribeType<ValueWideEnum>().enumBytes, 4U);
    }

    TEST(ProgramInterpreterTest, AnEnumLoadZeroExtendsAndKeepsTheLowBits) {
        ValueWorld world;
        const auto index = [&](StringView path, const TypeDesc& desc, uint8_t bytes) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadSlot(reg, ProgramType::Enum, Link(world, path, desc), bytes);
            return Run<ProgramEnum>(builder.Finish(ProgramType::Enum, reg).value(), world.context).index;
        };
        world.caster.attack = -1;
        EXPECT_EQ(index("caster.attack", DescribeType<int32_t>(), 4), 0xFFFFFFFFU) << "four bytes, zero-extended: no sign";
        EXPECT_EQ(index("caster.attack", DescribeType<int32_t>(), 2), 0xFFFFU);
        EXPECT_EQ(index("caster.attack", DescribeType<int32_t>(), 1), 0xFFU);
        world.caster.score = 0x100000002LL;
        EXPECT_EQ(index("caster.score", DescribeType<int64_t>(), 8), 2U) << "the low 32 bits of a wider value";
    }

    TEST(ProgramInterpreterTest, TheBuilderRefusesAnEnumLoadOfAnOddSize) {
        ValueWorld world;
        for (const uint8_t bytes : {uint8_t{0}, uint8_t{3}, uint8_t{16}}) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.Emit(Opcode::LoadSlotEnum, reg, 0, bytes, 0, 0);
            const auto program = builder.Finish(ProgramType::Enum, reg);
            ASSERT_FALSE(program.has_value()) << static_cast<int>(bytes);
            EXPECT_NE(program.error().message.find("enum field"), String::npos) << program.error().message;
        }
    }

    TEST(ProgramInterpreterTest, EnumsAreTheirIndexAndRunReturnsTheEnumerator) {
        static_assert(kProgramTypeOf<ValueElement> == ProgramType::Enum && kProgramTypeOf<ProgramEnum> == ProgramType::Enum);
        static_assert(kProgramTypeOf<PoolString> == ProgramType::String && kProgramTypeOf<Vec3> == ProgramType::Vec3);
        static_assert(!ProgramValue<ValuePoint> && !ProgramValue<Rect> && !ProgramValue<String>, "a type outside the list is no program type");
        const auto values = EnumUtils<ValueElement>::Values();
        for (size_t i = 0; i < values.size(); ++i) {
            EXPECT_EQ(static_cast<size_t>(values[i]), i) << "the values of a CORE_ENUM are 0..N-1 in declaration order";
            EXPECT_EQ(Eval<ValueElement>(Opcode::Copy, values[i]), values[i]);
        }
        EXPECT_EQ(Eval<ValueElement>(Opcode::SelectEnum, true, ValueElement::Fire, ValueElement::Earth), ValueElement::Fire);
        EXPECT_EQ(Eval<ValueElement>(Opcode::SelectEnum, false, ValueElement::Fire, ValueElement::Earth), ValueElement::Earth);
        EXPECT_TRUE(Eval<bool>(Opcode::EqEnum, ValueElement::Water, ValueElement::Water));
        EXPECT_TRUE(Eval<bool>(Opcode::NeEnum, ValueElement::Water, ValueElement::Fire));
    }

    TEST(ProgramInterpreterTest, EqualityOfTheVectorsIsExactAndNanIsNeverEqual) {
        const Vec3 withNan{kNaN, 1.0f, 2.0f};
        EXPECT_FALSE(Eval<bool>(Opcode::EqVec3, withNan, withNan));
        EXPECT_TRUE(Eval<bool>(Opcode::NeVec3, withNan, withNan));
        EXPECT_TRUE(Eval<bool>(Opcode::EqVec3, Vec3{-0.0f, 1.0f, 2.0f}, Vec3{0.0f, 1.0f, 2.0f})) << "-0 equals 0 as in C++";
        EXPECT_FALSE(Eval<bool>(Opcode::EqVec3, Vec3{0.0f, 1.0f, 2.0f}, Vec3{0.0f, 1.0f, 2.5f}));
        EXPECT_FALSE(Eval<bool>(Opcode::EqQuat, Quat{0.0f, 0.0f, 0.0f, 1.0f}, Quat{0.0f, 0.0f, 0.0f, -1.0f})) << "q and -q are not equal";
        EXPECT_FALSE(Eval<bool>(Opcode::EqColor, Color{1, 2, 3, 4}, Color{1, 2, 3, 5}));
        EXPECT_TRUE(Eval<bool>(Opcode::EqString, PoolString::Intern("Fire"), PoolString::Intern("Fire")));
        EXPECT_FALSE(Eval<bool>(Opcode::EqString, PoolString::Intern("Fire"), PoolString{}));
        EXPECT_TRUE(Eval<bool>(Opcode::EqString, PoolString{}, PoolString::Intern(""))) << "the empty text is one string";
    }

    TEST(ProgramInterpreterTest, AnOperationAProgramTypeLacksHasNoRow) {
        EXPECT_EQ(OpcodeOf(ProgramOperation::Add, ProgramType::Bool), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Add, ProgramType::String), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Add, ProgramType::Enum), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lt, ProgramType::Vec3), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Lt, ProgramType::String), Opcode::Invalid);
        EXPECT_EQ(OpcodeOf(ProgramOperation::Neg, ProgramType::Color), Opcode::Invalid);
        for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
            const String name(EnumUtils<ProgramType>::ToString(type));
            EXPECT_NE(OpcodeOf(ProgramOperation::LoadSlot, type), Opcode::Invalid) << name;
            EXPECT_NE(OpcodeOf(ProgramOperation::Eq, type), Opcode::Invalid) << name;
            EXPECT_NE(OpcodeOf(ProgramOperation::Ne, type), Opcode::Invalid) << name;
            EXPECT_NE(OpcodeOf(ProgramOperation::Select, type), Opcode::Invalid) << name;
            EXPECT_EQ(OpcodeOf(ProgramOperation::LoadSlot, type), LoadSlotOpcode(type)) << name;
        }
    }

    TEST(ProgramInterpreterTest, TheResultTypeOfAnOpcodeComesFromItsKernel) {
        static_assert(ResultTypeOf(Opcode::EqVec3) == ProgramType::Bool);
        static_assert(ResultTypeOf(Opcode::LtInt64) == ProgramType::Bool);
        static_assert(ResultTypeOf(Opcode::SelectVec3) == ProgramType::Vec3);
        static_assert(ResultTypeOf(Opcode::LoadSlotQuat) == ProgramType::Quat);
        static_assert(ResultTypeOf(Opcode::LoadSlotEnum) == ProgramType::Enum);
        static_assert(ResultTypeOf(Opcode::AddInt32) == ProgramType::Int32);
        static_assert(ResultTypeOf(Opcode::FloatToInt32) == ProgramType::Int32);
        static_assert(!ResultTypeOf(Opcode::LoadConst).has_value() && !ResultTypeOf(Opcode::Copy).has_value() && !ResultTypeOf(Opcode::Invalid).has_value());
        for (const Opcode op : EnumUtils<Opcode>::Values()) {
            if (op != Opcode::Invalid && op != Opcode::LoadConst && op != Opcode::Copy) {
                EXPECT_TRUE(ResultTypeOf(op).has_value()) << String(EnumUtils<Opcode>::ToString(op)).c_str();
            }
        }
    }

    TEST(ProgramInterpreterTest, EveryProgramTypeFitsARegister) {
        EXPECT_EQ(ProgramTypeSize(ProgramType::Vec2), sizeof(Vec2));
        EXPECT_EQ(ProgramTypeSize(ProgramType::Vec3), 12U);
        EXPECT_EQ(ProgramTypeSize(ProgramType::Vec4), 16U);
        EXPECT_EQ(ProgramTypeSize(ProgramType::Quat), 16U);
        EXPECT_EQ(ProgramTypeSize(ProgramType::Color), 4U);
        EXPECT_EQ(ProgramTypeSize(ProgramType::Enum), sizeof(uint32_t));
        EXPECT_EQ(ProgramTypeSize(ProgramType::String), sizeof(PoolString));
        for (const ProgramType type : EnumUtils<ProgramType>::Values()) {
            EXPECT_GT(ProgramTypeSize(type), 0U);
            EXPECT_LE(ProgramTypeSize(type), kProgramRegisterBytes);
        }
    }

    TEST(ProgramInterpreterTest, ConstantsOfTheNewTypesAreZeroPaddedAndInternedByBytes) {
        const auto constantProgram = [](auto value) {
            ProgramBuilder builder;
            const uint8_t reg = builder.AcquireRegister().value();
            builder.EmitLoadConst(reg, value);
            return builder.Finish(kProgramTypeOf<decltype(value)>, reg).value();
        };
        const Program vec = constantProgram(Vec3{1.0f, 2.0f, 3.0f});
        ASSERT_EQ(vec.Constants().size(), 1U);
        const Vec3 expected{1.0f, 2.0f, 3.0f};
        EXPECT_EQ(std::memcmp(vec.Constants()[0].bytes.data(), &expected, sizeof(expected)), 0);
        for (size_t i = sizeof(expected); i < kProgramRegisterBytes; ++i) {
            EXPECT_EQ(vec.Constants()[0].bytes[i], 0) << i;
        }
        EXPECT_EQ(ProgramPool::Intern(constantProgram(Vec3{1.0f, 2.0f, 3.0f})).value(), ProgramPool::Intern(constantProgram(Vec3{1.0f, 2.0f, 3.0f})).value());
        EXPECT_NE(ProgramPool::Intern(constantProgram(Vec3{1.0f, 2.0f, 3.0f})).value(), ProgramPool::Intern(constantProgram(Vec3{1.0f, 2.0f, 4.0f})).value());
        EXPECT_EQ(ProgramPool::Intern(constantProgram(PoolString::Intern("burn"))).value(), ProgramPool::Intern(constantProgram(PoolString::Intern("burn"))).value());
        EXPECT_NE(ProgramPool::Intern(constantProgram(PoolString::Intern("burn"))).value(), ProgramPool::Intern(constantProgram(PoolString::Intern("chill"))).value());
        EXPECT_NE(ProgramPool::Intern(constantProgram(ValueElement::Fire)).value(), ProgramPool::Intern(constantProgram(ValueElement::Water)).value());
        EXPECT_EQ(Run<ValueElement>(constantProgram(ValueElement::Earth), Context{}), ValueElement::Earth);
        EXPECT_EQ(Run<PoolString>(constantProgram(PoolString::Intern("burn")), Context{}), PoolString::Intern("burn"));
        EXPECT_NE(constantProgram(Vec3{1.0f, 2.0f, 3.0f}).Disassemble().find("Vec3"), String::npos);
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
