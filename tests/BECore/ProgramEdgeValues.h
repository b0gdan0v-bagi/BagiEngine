#pragma once

#include <BECore/Math/Color.h>
#include <BECore/Math/IVec2.h>
#include <BECore/Math/Quat.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Math/Vec3.h>
#include <BECore/Math/Vec4.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/PoolString/StaticPoolString.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Opcode.h>
#include <cstdint>
#include <limits>

namespace BECore::Tests {

    // The inputs the tests of the instruction set feed every operation: zeros, ones, the extremes of each type, NaN and the infinities. `ProgramInterpreterTest` checks
    // the rows against a C++ reference with them and `NativeProgramEqualityTest` runs both paths over them.

    inline constexpr int32_t kInt32Min = std::numeric_limits<int32_t>::min();
    inline constexpr int32_t kInt32Max = std::numeric_limits<int32_t>::max();
    inline constexpr int64_t kInt64Min = std::numeric_limits<int64_t>::min();
    inline constexpr int64_t kInt64Max = std::numeric_limits<int64_t>::max();
    inline constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    inline constexpr float kInf = std::numeric_limits<float>::infinity();
    inline constexpr double kDoubleNaN = std::numeric_limits<double>::quiet_NaN();
    inline constexpr double kDoubleInf = std::numeric_limits<double>::infinity();

    template <typename T>
    Vector<T> Edges();

    template <>
    inline Vector<bool> Edges<bool>() {
        return {false, true};
    }

    template <>
    inline Vector<int32_t> Edges<int32_t>() {
        return {0, 1, -1, 2, -2, 7, -7, 100, 65536, kInt32Min, kInt32Min + 1, kInt32Max, kInt32Max - 1};
    }

    template <>
    inline Vector<int64_t> Edges<int64_t>() {
        return {0, 1, -1, 2, -2, 7, -7, 100, 4294967296, kInt64Min, kInt64Min + 1, kInt64Max, kInt64Max - 1, static_cast<int64_t>(kInt32Min), static_cast<int64_t>(kInt32Max)};
    }

    template <>
    inline Vector<float> Edges<float>() {
        return {0.0f, 1.0f,  -1.0f, 0.5f, -0.5f, 1.5f, -1.5f, 2.5f, -2.5f, 3.0f, -3.25f, std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::min(),
                kInf, -kInf, kNaN};
    }

    template <>
    inline Vector<double> Edges<double>() {
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
    inline Vector<Vec2> Edges<Vec2>() {
        return {Vec2{0.0f, 0.0f}, Vec2{1.0f, 2.0f}, Vec2{1.0f, 3.0f}, Vec2{2.0f, 2.0f}, Vec2{kNaN, 0.0f}, Vec2{0.0f, kNaN}, Vec2{kInf, -kInf}, Vec2{-0.0f, 0.0f}};
    }

    template <>
    inline Vector<Vec3> Edges<Vec3>() {
        return {Vec3{0.0f, 0.0f, 0.0f}, Vec3{1.0f, 2.0f, 3.0f}, Vec3{1.0f, 2.0f, 4.0f},  Vec3{1.0f, 3.0f, 3.0f}, Vec3{2.0f, 2.0f, 3.0f},
                Vec3{kNaN, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, kNaN}, Vec3{kInf, -kInf, 0.0f}, Vec3{-0.0f, 0.0f, 0.0f}};
    }

    template <>
    inline Vector<Vec4> Edges<Vec4>() {
        return {Vec4{0.0f, 0.0f, 0.0f, 0.0f}, Vec4{1.0f, 2.0f, 3.0f, 4.0f}, Vec4{1.0f, 2.0f, 3.0f, 5.0f},  Vec4{1.0f, 2.0f, 4.0f, 4.0f},
                Vec4{kNaN, 0.0f, 0.0f, 0.0f}, Vec4{0.0f, 0.0f, 0.0f, kNaN}, Vec4{kInf, -kInf, 0.0f, 1.0f}, Vec4{0.0f, -0.0f, 0.0f, 0.0f}};
    }

    template <>
    inline Vector<IVec2> Edges<IVec2>() {
        return {IVec2{0, 0}, IVec2{1, 2}, IVec2{1, 3}, IVec2{2, 2}, IVec2{kInt32Min, kInt32Max}, IVec2{kInt32Max, kInt32Min}, IVec2{-1, -1}};
    }

    template <>
    inline Vector<Quat> Edges<Quat>() {
        return {Quat{0.0f, 0.0f, 0.0f, 1.0f}, Quat{0.0f, 0.0f, 0.0f, 0.0f},  Quat{0.0f, 0.0f, 0.70710678f, 0.70710678f}, Quat{0.0f, 0.0f, 0.70710678f, -0.70710678f}, Quat{1.0f, 0.0f, 0.0f, 0.0f},
                Quat{0.0f, 0.0f, 0.0f, kNaN}, Quat{0.0f, -0.0f, 0.0f, 1.0f}, Quat{0.0f, 0.0f, 0.01f, 0.99995f}};
    }

    template <>
    inline Vector<Color> Edges<Color>() {
        return {Color{0, 0, 0, 0}, Color{255, 255, 255, 255}, Color{10, 20, 30, 255}, Color{10, 20, 30, 254}, Color{10, 20, 31, 255}, Color{11, 20, 30, 255}, Color{10, 21, 30, 255}};
    }

    template <>
    inline Vector<ProgramEnum> Edges<ProgramEnum>() {
        return {ProgramEnum{0}, ProgramEnum{1}, ProgramEnum{2}, ProgramEnum{3}, ProgramEnum{std::numeric_limits<uint32_t>::max()}};
    }

    /// Images that differ in one byte of either half, and the full ones: a compare of fewer than 32 bytes would miss one
    template <>
    inline Vector<ProgramDomainValue> Edges<ProgramDomainValue>() {
        ProgramDomainValue zero;
        ProgramDomainValue lowByte;
        lowByte.bytes.front() = 1;
        ProgramDomainValue highByte;
        highByte.bytes.back() = 1;
        ProgramDomainValue lowHalf;
        ProgramDomainValue highHalf;
        ProgramDomainValue full;
        for (size_t i = 0; i < kProgramRegisterBytes; ++i) {
            const auto byte = static_cast<uint8_t>(0xA0 + i);
            (i < kProgramRegisterBytes / 2 ? lowHalf : highHalf).bytes[i] = byte;
            full.bytes[i] = byte;
        }
        return {zero, lowByte, highByte, lowHalf, highHalf, full};
    }

    template <>
    inline Vector<PoolString> Edges<PoolString>() {
        return {PoolString{}, ""_intern, "Fire"_intern, "fire"_intern, "Fire "_intern, "Water"_intern, "a longer text that is not a name of anything"_intern};
    }

}  // namespace BECore::Tests
