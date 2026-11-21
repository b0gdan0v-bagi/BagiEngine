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

    // The formulas with loops (`Values/Programs/Lists/`). They read `hand`, a `ProgramListHand` (`kListsLevel`): `tiles` is a full array of objects, `sets` a vector of objects that hold
    // an array, `counts`, `weights` and `flags` are lists of scalars, `none` is always empty, `before` and `a.i32` give an index. Every fold and every result type is here, and an index that
    // may be outside its list.

    inline constexpr StringView kNativeListIntFormulas[] = {
        "count(hand.tiles, t, t.terminal)",
        "count(hand.tiles, t, t.suit == Dragons || t.rank > a.i32)",
        "count(hand.sets, s, s.pung)",
        "count(hand.counts, n, n > hand.before)",
        "count(hand.flags, f, f)",
        "sum(hand.tiles, t, t.rank)",
        "sum(hand.counts, n, n * 2 + 1)",
        "sum(hand.flags, f, select(f, 1, 0))",
        "sum(hand.none, t, t.rank)",
        "min(hand.tiles, t, t.rank)",
        "max(hand.tiles, t, t.rank)",
        "min(hand.counts, n, n)",
        "max(hand.counts, n, n)",
        "size(hand.sets)",
        "size(hand.counts) + size(hand.weights) * 100 + size(hand.none)",
        "hand.counts[hand.before]",
        "hand.counts[a.i32] + hand.counts[b.i32 - 1]",
        "hand.tiles[hand.before].rank",
        "hand.sets[hand.before].tiles[a.i32].rank",
        "sum(hand.sets, s, count(s.tiles, t, t.terminal))",
        "sum(hand.sets, s, sum(s.tiles, t, t.rank + hand.before))",
        "max(hand.sets, s, min(s.tiles, t, t.rank))",
        "count(hand.sets, s, any(s.tiles, t, t.rank > a.i32))",
    };

    inline constexpr StringView kNativeListInt64Formulas[] = {
        "sum(hand.counts, n, int64(n) * 4000000000)",
        "max(hand.tiles, t, int64(t.rank) * 3)",
        "sum(hand.sets, s, sum(s.tiles, t, int64(t.rank)))",
    };

    inline constexpr StringView kNativeListFloatFormulas[] = {
        "sum(hand.weights, w, w)",
        "min(hand.weights, w, w)",
        "max(hand.weights, w, w)",
        "sum(hand.tiles, t, t.weight * 0.5)",
        "min(hand.tiles, t, t.weight)",
        "max(hand.tiles, t, t.weight)",
        "sum(hand.counts, n, float(n))",
        "hand.weights[hand.before]",
        "hand.tiles[a.i32].weight",
        "hand.tiles[hand.before].position.x",
        "sum(hand.sets, s, max(s.tiles, t, t.weight))",
    };

    inline constexpr StringView kNativeListDoubleFormulas[] = {
        "sum(hand.weights, w, double(w))",
        "min(hand.tiles, t, double(t.weight) * 2)",
        "max(hand.counts, n, double(n) / 3)",
    };

    inline constexpr StringView kNativeListBoolFormulas[] = {
        "any(hand.tiles, t, t.rank > 8)",
        "any(hand.tiles, t, t.terminal)",
        "all(hand.tiles, t, t.terminal)",
        "all(hand.tiles, t, t.suit == hand.tiles[0].suit)",
        "any(hand.sets, s, s.pung)",
        "all(hand.sets, s, s.pung)",
        "all(hand.counts, n, n > 0)",
        "any(hand.weights, w, w > 1.5)",
        "any(hand.flags, f, f)",
        "all(hand.flags, f, f)",
        "any(hand.none, t, t.terminal)",
        "all(hand.none, t, t.terminal)",
        "hand.flags[hand.before]",
        "hand.tiles[a.i32].terminal",
        "hand.sets[hand.before].pung && size(hand.sets) > 1",
        "any(hand.sets, s, all(s.tiles, t, t.terminal))",
        "all(hand.sets, s, any(s.tiles, t, t.suit == s.tiles[0].suit && t.rank > 3))",
    };

    // The patterns of the Mahjong proof (tests/BECore/ValueListMahjongTest.cpp) over `mahjong`, a `ValueListHand` (`kMahjongLevel`): the set compiles them so the proof also runs in generated code.

    inline constexpr StringView kNativeMahjongBoolFormulas[] = {
        "count(mahjong.sets, s, s.pung && s.dragon) == 3",
        "all(mahjong.tiles, t, t.suit == mahjong.tiles[0].suit)",
        "any(mahjong.yaku, y, y.on)",
        "mahjong.sets[0].suit == mahjong.sets[1].suit",
    };

    inline constexpr StringView kNativeMahjongIntFormulas[] = {
        "count(mahjong.tiles, t, t.terminal)",          "count(mahjong.tiles, t, t.honor)", "count(mahjong.tiles, t, t.terminal || t.honor)", "sum(mahjong.sets, s, s.fu)",
        "sum(mahjong.yaku, y, select(y.on, y.han, 0))",
    };

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
