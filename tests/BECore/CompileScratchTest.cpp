// The heap cost of compiling formulas and scripts (Values/Programs, Values/Effects): what the front end allocates on the heap (the profile preset counts) and a benchmark shaped like a
// content load, run by hand.

#include <BECore/EffectsToys.h>
#include <BECore/Memory/ScratchArena.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Effects/EffectCompiler.h>
#include <BECore/Values/Effects/ScriptParser.h>
#include <BECore/Values/Programs/ExprParser.h>
#include <BECore/Values/Programs/Program.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/AllocCheck.h>
#include <Common/Require.h>
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
        /// The heap allocations a second compile of the formula of the budget case may make. What remains is the linking of the five paths of the formula (the reflection's path
        /// compile, `Reflection/`) and the output: the `Program` handed to the pool (found there, so dropped) and the result's path
        constexpr uint64_t kCompileAllocBudget = 112;
        /// The same for a second compile of the script of the budget case. The script is compiled in two passes, so each of its paths is linked twice by the reflection; the rest is the
        /// output (the `EffectProgram`, its source text, a `CompiledExpression::path` per expression)
        constexpr uint64_t kEffectsAllocBudget = 320;

        /// Distinct programs a pool case interns: a content load is a few hundred small programs
        constexpr uint32_t kPoolPrograms = 300;
        /// The pool's name in the arena counters; no entry exists while its programs are on the heap
        constexpr StringView kPoolArenaName = "ProgramPool";
        /// The heap blocks the pool may keep per interned program: one node of its hash table, the code and the constants being in its arena
        constexpr uint64_t kHeldBlocksPerProgram = 1;
        /// The blocks that do not grow with the number of programs: the arena's chunks, the growth of the pool's tables
        constexpr uint64_t kHeldBlocksSlack = 48;

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

        /// What the pool holds after @p programs distinct programs were interned
        struct PoolCounts {
            uint32_t programs = 0;
            int64_t heldBlocks = 0;
            int64_t heldBytes = 0;
            uint64_t arenaBytes = 0;
        };

        int64_t LiveBlocks(const AllocTotals& totals) {
            return static_cast<int64_t>(totals.count) - static_cast<int64_t>(totals.frees);
        }

        /// Interns @p count programs no earlier run interned (each loads a constant of its own, from a fresh salt) and measures what the pool keeps on the heap: the temporaries of an
        /// iteration are freed by its end, so the difference of the live blocks is what the pool holds
        PoolCounts MeasurePool(uint32_t count) {
            const int32_t base = static_cast<int32_t>(NextSalt() * kSaltStride);
            const AllocTotals before = AllocTracker::GetTotals();
            for (uint32_t i = 0; i < count; ++i) {
                ProgramBuilder builder;
                const uint8_t dst = Require(builder.AcquireRegister());
                builder.EmitLoadConst(dst, base + static_cast<int32_t>(i));
                const auto program = builder.Finish(kProgramTypeOf<int32_t>, dst);
                EXPECT_TRUE(program.has_value());
                if (program.has_value()) {
                    EXPECT_TRUE(ProgramPool::Intern(*program).has_value());
                }
            }
            const AllocTotals after = AllocTracker::GetTotals();
            PoolCounts counts;
            counts.programs = count;
            counts.heldBlocks = LiveBlocks(after) - LiveBlocks(before);
            counts.heldBytes = static_cast<int64_t>(after.liveBytes) - static_cast<int64_t>(before.liveBytes);
            for (const ArenaStatsEntry& entry : GetArenaStats()) {
                if (StringView(entry.name) == kPoolArenaName) {
                    counts.arenaBytes = entry.used;
                }
            }
            return counts;
        }

        void PrintPool(const PoolCounts& counts) {
            std::cout << "CompilePool programs=" << counts.programs << " heldBlocks=" << counts.heldBlocks << " heldBytes=" << counts.heldBytes << " arenaBytes=" << counts.arenaBytes << "\n";
        }

        void Print(const char* name, const CompileCounts& counts) {
            std::cout << name << " formulas=" << counts.texts << " allocs=" << counts.allocs << " bytes=" << counts.bytes << " secondAllocs=" << counts.secondAllocs << " ms=" << counts.ms << "\n";
        }

    }  // namespace

    TEST(CompileScratchTest, ParsingAFormulaInAScopeAllocatesNothingOnTheHeap) {
        constexpr StringView kFormula = "clamp(caster.attack * 2 + cast.level, 0, 100) - abs(lerp(caster.speed, cast.power, 0.25))";
        {
            // The first use of the thread's arena takes its chunk from the heap
            const ScratchScope warmUp;
            EXPECT_TRUE(ParseExpression(kFormula).has_value());
        }
        ExpectNoAllocations([&] {
            const ScratchScope scratch;
            EXPECT_TRUE(ParseExpression(kFormula).has_value());
        });
    }

    TEST(CompileScratchTest, ASecondCompileOfAFormulaStaysWithinTheAllocationBudget) {
        constexpr StringView kFormula = "select(caster.alive, caster.speed * 3, cast.power) + lerp(caster.speed, cast.power, 0.25)";
        const ValueWorld world;
        const TypeDesc& type = DescribeType<float>();
        // The first compile adds the program to the pool and takes the arena's first chunk
        ASSERT_TRUE(CompileExpression(kFormula, world.declaration, type).has_value());
        const AllocCount second = CountAllocations([&] { EXPECT_TRUE(CompileExpression(kFormula, world.declaration, type).has_value()); });
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(second.count, kCompileAllocBudget);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(CompileScratchTest, ParsingAScriptInAScopeAllocatesNothingOnTheHeap) {
        constexpr StringView kScript = "if (caster.attack > 3) { caster.attack -= 2; emit FxCount(caster.attack + 1); } else { caster.speed++; } toggle caster.alive;";
        {
            const ScratchScope warmUp;
            EXPECT_TRUE(ParseScript(kScript).has_value());
        }
        ExpectNoAllocations([&] {
            const ScratchScope scratch;
            EXPECT_TRUE(ParseScript(kScript).has_value());
        });
    }

    TEST(CompileScratchTest, ASecondCompileOfAScriptStaysWithinTheAllocationBudget) {
        constexpr StringView kScript = "caster.attack = caster.attack * 2 + 7; caster.speed -= 0.5; if (caster.alive) { caster.attack++; emit FxCount(cast.level); }";
        const EffectsWorld effects;
        ASSERT_TRUE(CompileEffects(kScript, effects.world.declaration, effects.options).has_value());
        const AllocCount second = CountAllocations([&] { EXPECT_TRUE(CompileEffects(kScript, effects.world.declaration, effects.options).has_value()); });
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_LE(second.count, kEffectsAllocBudget);
        } else {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

    TEST(CompileScratchTest, InterningProgramsLeavesOnlyTheirTableNodesOnTheHeap) {
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
        // The first program takes the pool's first chunks, which a long-lived process has long had
        (void)MeasurePool(1);
        const PoolCounts counts = MeasurePool(kPoolPrograms);
        EXPECT_LE(counts.heldBlocks, static_cast<int64_t>(kPoolPrograms * kHeldBlocksPerProgram + kHeldBlocksSlack)) << "heldBytes=" << counts.heldBytes;
    }

    /// A benchmark, not a check: the counts are zero without the profile preset (`kNoAllocTracker`)
    TEST(CompileScratchBenchTest, DISABLED_CompileOfFormulasAndScripts) {
        Print("CompileScratch", MedianOf([] { return MeasureFormulas(); }));
        Print("CompileScratchEffects", MedianOf([] { return MeasureScripts(); }));
        (void)MeasurePool(1);
        Vector<PoolCounts> pools;
        for (int i = 0; i < kRepetitions; ++i) {
            pools.push_back(MeasurePool(kPoolPrograms));
        }
        std::sort(pools.begin(), pools.end(), [](const PoolCounts& a, const PoolCounts& b) { return a.heldBlocks < b.heldBlocks; });
        PrintPool(pools[pools.size() / 2]);
        if constexpr (!AllocTracker::kEnabled) {
            GTEST_SKIP() << kNoAllocTracker;
        }
    }

}  // namespace BECore::Tests
