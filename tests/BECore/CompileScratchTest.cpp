// The heap cost of compiling formulas and scripts (Values/Programs, Values/Effects): a benchmark shaped like a content load, run by hand with the profile preset.

#include <BECore/EffectsToys.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Effects/EffectCompiler.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/AllocCheck.h>
#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <iostream>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr uint32_t kCopies = 60;
        constexpr uint32_t kScriptCopies = 40;
        constexpr uint32_t kSaltStride = 100000;
        constexpr char kCopyMark = '$';
        constexpr int kRepetitions = 5;

        struct FormulaShape {
            StringView text;
            const TypeDesc* type;
        };

        /// The text of @p shape with each `$` replaced by @p number, so a copy is a text the pool has not seen
        String Numbered(StringView shape, uint32_t number) {
            String text;
            for (const char c : shape) {
                if (c == kCopyMark) {
                    text.append(Format("{}", number));
                } else {
                    text.push_back(c);
                }
            }
            return text;
        }

        /// The shapes of the formula tests: arithmetic, clamps, selects, booleans, doubles and vectors over `caster` and `cast`
        const Vector<FormulaShape>& FormulaShapes() {
            static const Vector<FormulaShape> shapes = {
                {"caster.attack * (100 + cast.level * $) / 100 + $", &DescribeType<int32_t>()},
                {"clamp(caster.attack, 0, cast.level * 5 + $) - max(min(caster.attack, $), min(cast.level, 5))", &DescribeType<int32_t>()},
                {"select(caster.alive, caster.speed * $, cast.power) + lerp(caster.speed, cast.power, 0.25)", &DescribeType<float>()},
                {"caster.alive && cast.level > $ || caster.speed < cast.power && !caster.alive", &DescribeType<bool>()},
                {"caster.precise * $ + sqrt(2.0) + round(caster.precise) - ceil(double(cast.power))", &DescribeType<double>()},
                {"caster.velocity * $ + vec3(1, 2, $) * cast.power", &DescribeType<Vec3>()},
                {"caster.score / 3 + int64(cast.level) * $", &DescribeType<int64_t>()},
            };
            return shapes;
        }

        /// Scripts of the effect tests' kinds: branches, an else-if chain, compound stores, a toggle and an emit
        const Vector<StringView>& ScriptShapes() {
            static const Vector<StringView> shapes = {
                "if (caster.attack > $) { caster.attack -= 3; caster.score += int64(caster.attack); } else if (caster.speed > 1.5) { caster.speed = caster.speed * 2.0; "
                "emit FxCount(caster.attack + $); } else { caster.attack++; } toggle caster.alive; caster.speed += cast.power * 2;",
                "caster.attack = caster.attack * 2 + $; caster.speed -= 0.5; caster.precise += 0.25; caster.velocity += vec3(1, 1, $); caster.score -= int64(caster.attack);",
                "if (cast.level > $) { caster.attack = $; if (caster.alive) { toggle caster.alive; } } if (caster.attack == $) { emit FxCount(cast.level); emit FxPing(); }",
                "caster.attack++; caster.attack++; caster.speed--; emit FxCount(clamp(caster.attack, 0, $));",
            };
            return shapes;
        }

        struct CompileCounts {
            uint64_t allocs = 0;
            uint64_t bytes = 0;
            uint64_t secondAllocs = 0;
            double ms = 0.0;
            size_t texts = 0;
        };

        struct EffectsWorld {
            ValueWorld world;
            WriteTargets targets;
            EffectOptions options;

            EffectsWorld() {
                (void)EffectsData::RegisterEffectToys();
                EXPECT_TRUE(targets.Allow("caster"));
                options.targets = &targets;
            }
        };

        /// Compiles every text of @p texts twice (the second compile finds the program in the pool) and counts what the calls allocated
        template <typename TCompile>
        CompileCounts MeasureCompiles(const Vector<String>& texts, TCompile&& compile) {
            CompileCounts counts;
            counts.texts = texts.size();
            const auto start = std::chrono::steady_clock::now();
            const AllocCount first = CountAllocations([&] {
                for (const String& text : texts) {
                    if (!compile(text)) {
                        ADD_FAILURE() << text.c_str();
                    }
                }
            });
            const AllocCount second = CountAllocations([&] {
                for (const String& text : texts) {
                    if (!compile(text)) {
                        ADD_FAILURE() << text.c_str();
                    }
                }
            });
            const auto stop = std::chrono::steady_clock::now();
            counts.allocs = first.count + second.count;
            counts.bytes = first.bytes + second.bytes;
            counts.secondAllocs = second.count;
            counts.ms = std::chrono::duration<double, std::milli>(stop - start).count();
            return counts;
        }

        /// The texts of one run: each run takes a salt of its own, so the first compile of a text adds a program to the pool
        uint32_t NextSalt() {
            static uint32_t salt = 0;
            return ++salt;
        }

        CompileCounts MeasureFormulas() {
            const uint32_t base = NextSalt() * kSaltStride;
            Vector<String> texts;
            Vector<const TypeDesc*> types;
            for (uint32_t copy = 0; copy < kCopies; ++copy) {
                for (const FormulaShape& shape : FormulaShapes()) {
                    texts.push_back(Numbered(shape.text, base + copy));
                    types.push_back(shape.type);
                }
            }
            const ValueWorld world;
            size_t index = 0;
            return MeasureCompiles(texts, [&](const String& text) {
                const TypeDesc& type = *types[index % types.size()];
                ++index;
                return CompileExpression(text, world.declaration, type).has_value();
            });
        }

        CompileCounts MeasureScripts() {
            const uint32_t base = NextSalt() * kSaltStride;
            Vector<String> texts;
            for (uint32_t copy = 0; copy < kScriptCopies; ++copy) {
                for (const StringView shape : ScriptShapes()) {
                    texts.push_back(Numbered(shape, base + copy));
                }
            }
            const EffectsWorld effects;
            return MeasureCompiles(texts, [&](const String& text) { return CompileEffects(text, effects.world.declaration, effects.options).has_value(); });
        }

        template <typename TMeasure>
        CompileCounts MedianOf(TMeasure&& measure) {
            Vector<CompileCounts> runs;
            for (int i = 0; i < kRepetitions; ++i) {
                runs.push_back(measure());
            }
            std::sort(runs.begin(), runs.end(), [](const CompileCounts& a, const CompileCounts& b) { return a.ms < b.ms; });
            return runs[runs.size() / 2];
        }

        void Print(const char* name, const CompileCounts& counts) {
            std::cout << name << " formulas=" << counts.texts << " allocs=" << counts.allocs << " bytes=" << counts.bytes << " secondAllocs=" << counts.secondAllocs << " ms=" << counts.ms << "\n";
        }

    }  // namespace

    /// A benchmark, not a check: the counts are zero without the profile preset (`kNoAllocTracker`)
    TEST(CompileScratchBenchTest, DISABLED_CompileOfFormulasAndScripts) {
        Print("CompileScratch", MedianOf([] { return MeasureFormulas(); }));
        Print("CompileScratchEffects", MedianOf([] { return MeasureScripts(); }));
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
