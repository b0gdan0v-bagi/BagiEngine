#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Types/Types.h>
#include <BECore/Values/Programs/Instruction.h>

namespace BECore::Tests {

    // The formulas of the engine's native set, by the type each one computes. They read the levels of `NativeProgramSetDeclaration()`: `caster` and `cast` (the objects of
    // ValueBenchTest and ProgramCompilerTest) and `a`, `b`, `c`, which have a field of every program type, so each rule of the compiler that writes other code has a formula here:
    // folding, fusion (a field times a constant plus a constant, `lerp` of two fields, a table read from a field), registers up to the limit, tables, enums, components
    // and mixed operands. The set compiles each one fused and unfused.

    inline constexpr StringView kNativeIntFormulas[] = {
        "caster.attack * (100 + cast.level * 10) / 100",
        "caster.attack * 3 + 7",
        "clamp(caster.attack, 0, cast.level * 5)",
        "select(caster.alive, caster.attack, -caster.attack)",
        "abs(caster.attack) % (cast.level + 1) - 3",
        "a.i32 * 3 + 7",
        "b.i32 * 2 - 5 + c.i32",
        "a.i32 + b.i32 * c.i32",
        "-a.i32 / (b.i32 + 1)",
        "a.i32 % 7 + a.i32b % -3",
        "min(a.i32, a.i32b) + max(b.i32, c.i32)",
        "clamp(a.i32, -100, 100)",
        "select(a.i32 > b.i32, a.i32, b.i32)",
        "select(a.flag && !b.flag, 1, 2)",
        "(1 + 2) * (a.i32 - 4)",
        "a.i32 * 2 * 3",
        "int(a.f32 * 2) + int(a.f64) + int(a.i64)",
        "a.iv2.x + a.iv2.y * 2",
        "a.color.r + a.color.a",
        "at(b.i32, 10, 20, 30, 40)",
        "at(a.i32 % 3 + 1, 1, 2, 3) - at(b.i32, 100, 200)",
        "at(c.i32, 1, 2, 3) * a.i32 + 5",
        "ByLevel('NativeDamage', a.i32)",
        "ByLevel('NativeDamage', cast.level) + caster.attack",
    };

    inline constexpr StringView kNativeInt64Formulas[] = {
        "caster.score / 3 + 1", "caster.score + int64(cast.level) * 4000000000", "a.i64 / 3 + 1", "a.i64 + int64(b.i32) * 4000000000", "a.i64 * 2 - b.i64", "at(b.i32, 5000000000, -1, 7)",
    };

    inline constexpr StringView kNativeFloatFormulas[] = {
        "lerp(caster.speed, cast.power, 0.25)",
        "clamp(caster.speed * cast.power, -1.5, 6)",
        "float(caster.attack) / 4 + floor(cast.power)",
        "a.f32 * 2.5 + 1",
        "lerp(a.f32, b.f32, 0.25)",
        "lerp(a.f32, a.f32b, c.f32)",
        "clamp(a.f32 * b.f32, -1.5, 6)",
        "float(a.i32) / 4 + floor(b.f32)",
        "ceil(a.f32) - round(b.f32) + sqrt(abs(c.f32))",
        "length(a.v3) + dot(a.v3, b.v3)",
        "a.v3.x * 2 + b.v2.y",
        "a.quat.w * a.v4.z",
        "float(a.i64) * 0.5",
        "at(b.i32, 1.5, -2.5, 0.25)",
        "ByLevel('NativeDamage', b.i32) * a.f32",
    };

    inline constexpr StringView kNativeDoubleFormulas[] = {
        "caster.precise * 2 + sqrt(2.0)",
        "round(caster.precise) + ceil(double(cast.power))",
        "a.f64 * 2 + 1",
        "lerp(a.f64, b.f64, 0.5)",
        "double(a.f32) * a.f64 + sqrt(2.0)",
        "floor(a.f64) - ceil(b.f64) + round(c.f64)",
        "ByLevel('NativeDamage', a.i32) + a.f64",
        "at(a.i32, 1.5, 0.1, 1e300)",
    };

    inline constexpr StringView kNativeBoolFormulas[] = {
        "caster.alive && cast.level > 2",
        "!caster.alive || caster.attack >= 12 && cast.power != 2.5",
        "a.flag && b.flag || !c.flag",
        "a.i32 < b.i32 && a.f32 >= b.f32",
        "a.i32 == b.i32 || a.i64 != b.i64",
        "a.f64 > b.f64",
        "a.i32 > 5 && a.i32 < 10",
        "a.text == b.text",
        "a.text != b.text",
        "a.v2 == b.v2",
        "a.v3 == b.v3",
        "a.v4 != b.v4",
        "a.iv2 == b.iv2",
        "a.quat == b.quat",
        "a.color == b.color",
        "a.narrow == High",
        "a.wide16 != b.wide16",
        "a.wide32 == b.wide32",
        "select(a.flag, b.flag, c.flag)",
        "at(a.i32, true, false, false, true)",
    };

    inline constexpr StringView kNativeVec2Formulas[] = {
        "a.v2 + b.v2", "a.v2 * 2 - b.v2", "lerp(a.v2, b.v2, 0.5)", "vec2(a.f32, b.f32 * 2)", "select(a.flag, a.v2, b.v2)",
    };

    inline constexpr StringView kNativeVec3Formulas[] = {
        "(caster.velocity + cast.target) * cast.power - caster.velocity * 0.5",
        "caster.velocity * 2 + vec3(1, 2, 3)",
        "a.v3 * 2 + vec3(1, 2, 3)",
        "(a.v3 + b.v3) * c.f32 - a.v3 * 0.5",
        "normalize(a.v3)",
        "a.quat * a.v3",
        "a.v3 / 2",
        "vec3(a.f32, b.f32, c.f32)",
        "lerp(a.v3, b.v3, a.f32)",
        "b.f32 * a.v3",
        "at(a.i32, vec3(1, 0, 0), vec3(0, 1, 0))",
    };

    inline constexpr StringView kNativeVec4Formulas[] = {
        "a.v4 + b.v4", "a.v4 * 2", "-a.v4", "normalize(a.v4)", "vec4(a.f32, a.f32b, b.f32, c.f32)", "lerp(a.v4, b.v4, 0.25)", "a.v4 / 4", "a.v4 - b.v4",
    };

    inline constexpr StringView kNativeIVec2Formulas[] = {
        "a.iv2 + b.iv2", "a.iv2 * 3", "3 * a.iv2", "ivec2(a.i32, b.i32 + 1)", "-a.iv2", "a.iv2 - b.iv2", "select(a.flag, a.iv2, b.iv2)",
    };

    inline constexpr StringView kNativeQuatFormulas[] = {
        "a.quat * b.quat", "normalize(a.quat)", "lerp(a.quat, b.quat, 0.5)", "quat(a.f32, a.f32b, b.f32, 1)", "select(a.flag, a.quat, b.quat)",
    };

    inline constexpr StringView kNativeColorFormulas[] = {
        "color(a.color.r, a.color.g + 5, 300, a.color.a)",
        "lerp(a.color, b.color, 0.5)",
        "select(a.flag, a.color, b.color)",
        "color(a.i32, b.i32, c.i32, a.i32b)",
    };

    /// The enums, one list for each width of a field; `Low`, `Mid` and `High` are the enumerators of all three
    inline constexpr StringView kNativeNarrowEnumFormulas[] = {"select(a.flag, a.narrow, High)", "select(a.i32 > 3, Low, b.narrow)", "at(a.i32, Low, Mid, High)"};
    inline constexpr StringView kNativeShortEnumFormulas[] = {"select(a.flag, a.wide16, High)", "at(b.i32, High, Low, Mid)"};
    inline constexpr StringView kNativeWideEnumFormulas[] = {"select(a.flag, a.wide32, b.wide32)", "at(c.i32, Mid, High, Low)"};

    /// A text is a program only as a choice between fields: the constant of a literal is the address of an entry of this process, which no generated function can hold
    inline constexpr StringView kNativeTextFormulas[] = {"select(a.flag, a.text, b.text)"};

    // The formulas over the toy mask and its operations (`ProgramDomainTypes.h`: the tool registers them before it compiles). Each calls the game's operations, so the
    // programs carry the fingerprint of the registry (`NativeProgramEntry::domainFingerprint`); `a`, `b` and `c` hold a mask and a pair each.

    inline constexpr StringView kNativeDomainBoolFormulas[] = {
        "popcount(or_mask(and_mask(a.mask, shift(b.mask, East)), c.mask)) > 3",
        "a.mask == b.mask",
        "any(and_mask(a.mask, c.mask)) && a.flag",
    };

    inline constexpr StringView kNativeDomainIntFormulas[] = {
        "popcount(shift(a.mask, South)) + a.i32 % 7",
        "popcount(bit(a.i32)) * 2 + popcount(cell(b.i32, c.i32))",
    };

    inline constexpr StringView kNativeDomainMaskFormulas[] = {
        "select(any(and_mask(a.mask, b.mask)), majority(a.mask, b.mask, c.mask), shift(c.mask, West))",
        "or_mask(a.mask, and_mask(shift(b.mask, North), c.mask))",
        "cell(a.i32, b.i32)",
    };

    inline constexpr StringView kNativeDomainPairFormulas[] = {"pair_of(a.i64, a.i64 * 2)", "select(a.flag, a.pair, pair_of(1, 2))"};

    /**
     * @brief A right-nested sum of `kMaxProgramRegisters - 1` additions: every level keeps one more register alive, so the program uses all of them
     *
     * The fields are alternated so that the sum reads three levels.
     */
    [[nodiscard]] inline String NativeDeepFormula() {
        constexpr StringView kFields[] = {"a.i32", "b.i32", "c.i32"};
        String text = "a.i32";
        for (size_t i = 0; i + 1 < kMaxProgramRegisters; ++i) {
            text = Format("{} + ({})", kFields[i % std::size(kFields)], text);
        }
        return text;
    }

}  // namespace BECore::Tests
