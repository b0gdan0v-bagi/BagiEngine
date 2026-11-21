// CachedValue (Values/CachedValue.h): the result of a formula kept while the version key of the levels it reads is the same. The formulas are compiled
// over the two levels of the context types of ValueTestTypes.h, each level with a counter its owner bumps; every result is compared with Value::Evaluate.

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/CachedValue.h>
#include <BECore/Values/Programs/ProgramBuilder.h>
#include <BECore/Values/Programs/ProgramCompiler.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;

        constexpr StringView kIntFormula = "caster.attack * (100 + cast.level * 10) / 100";
        constexpr StringView kFloatFormula = "caster.speed * cast.power + 1.5";
        constexpr StringView kVec3Formula = "caster.velocity * cast.power + cast.target";
        constexpr StringView kCasterOnlyFormula = "caster.attack * 3 + 7";

        /// Both levels of a `ValueWorld` with a counter each; `Rebuild` makes a context as a game does each frame
        struct CachedWorld {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            uint64_t casterVersion = 1;
            uint64_t castVersion = 1;
            Context context;

            CachedWorld() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                context = Rebuild();
            }

            CachedWorld(const CachedWorld&) = delete;
            CachedWorld& operator=(const CachedWorld&) = delete;

            [[nodiscard]] Context Rebuild() const {
                Context rebuilt(declaration);
                rebuilt.Set(kCasterLevel, caster, &casterVersion);
                rebuilt.Set(kCastLevel, cast, &castVersion);
                return rebuilt;
            }

            template <typename T>
            [[nodiscard]] Value<T> Compile(StringView text) const {
                const auto compiled = CompileValue<T>(text, declaration);
                EXPECT_TRUE(compiled.has_value()) << String(text).c_str();
                return compiled.value_or(Value<T>{});
            }
        };

        /// A program with no `Reads()` that computes 42: what the compiler would have folded to a `Const`
        Value<int32_t> HandAssembledConstant() {
            ProgramBuilder builder;
            const uint8_t reg = Require(builder.AcquireRegister());
            builder.EmitLoadConst(reg, int32_t{42});
            const auto program = builder.Finish(ProgramType::Int32, reg, "cached test: forty-two"_intern);
            EXPECT_TRUE(program.has_value());
            const Optional<ProgramId> id = ProgramPool::Intern(*program);
            EXPECT_TRUE(id.has_value());
            return Value<int32_t>::FromProgram(id.value_or(0), program->Source());
        }

        uint64_t Allocations() {
            return AllocTracker::GetThreadCounters().count;
        }

    }  // namespace

    static_assert(sizeof(Value<float>) == 24, "a cache is a class of its own: Value stays as it was");

    TEST(ValueCachedTest, CompileTimeChecks) {}

    TEST(ValueCachedTest, AConstAndASlotAreDelegatedAndNeverCached) {
        CachedWorld world;
        CachedValue<int32_t> constant(Value<int32_t>(5));
        EXPECT_EQ(constant.Evaluate(world.context), 5);

        Value<int32_t> slot;
        ASSERT_TRUE(slot.Link(world.declaration, "caster.attack").has_value());
        CachedValue<int32_t> cached(slot);
        world.caster.attack = 8;
        EXPECT_EQ(cached.Evaluate(world.context), 8);
        world.caster.attack = 9;
        EXPECT_EQ(cached.Evaluate(world.context), 9) << "a plain write is seen: a slot is read, not kept";

        EXPECT_EQ(constant.Computations(), 0U);
        EXPECT_EQ(cached.Computations(), 0U);
    }

    TEST(ValueCachedTest, AProgramRunsOnceWhileNothingItReadsChanges) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kIntFormula));
        ASSERT_EQ(cached.Source().Kind(), ValueKind::Program);
        const int32_t expected = world.caster.attack * (100 + world.cast.level * 10) / 100;
        for (int i = 0; i < 100; ++i) {
            EXPECT_EQ(cached.Evaluate(world.context), expected);
        }
        EXPECT_EQ(cached.Computations(), 1U);
    }

    TEST(ValueCachedTest, ABumpOfALevelItReadsRecomputesAndOneOfAnotherLevelDoesNot) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kCasterOnlyFormula));
        ASSERT_EQ(cached.Source().Reads(), LevelBit(kCasterLevel));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);

        world.cast.level = 50;
        ++world.castVersion;
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);
        EXPECT_EQ(cached.Computations(), 1U) << "the level it does not read moved";

        world.caster.attack = 20;
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7) << "a plain write with no bump is the owner's miss, not a change the cache can see";
        ++world.casterVersion;
        EXPECT_EQ(cached.Evaluate(world.context), 20 * 3 + 7);
        EXPECT_EQ(cached.Computations(), 2U);
    }

    TEST(ValueCachedTest, AnotherObjectOrAnotherCounterAtALevelItReadsRecomputes) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kCasterOnlyFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);

        ValueUnitStats other;
        other.attack = 100;
        Context withOther(world.declaration);
        withOther.Set(kCasterLevel, other, &world.casterVersion);
        withOther.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_EQ(cached.Evaluate(withOther), 100 * 3 + 7);
        EXPECT_EQ(cached.Computations(), 2U);

        uint64_t sameNumber = world.casterVersion;
        Context withOtherCounter(world.declaration);
        withOtherCounter.Set(kCasterLevel, other, &sameNumber);
        withOtherCounter.Set(kCastLevel, world.cast, &world.castVersion);
        other.attack = 200;
        EXPECT_EQ(cached.Evaluate(withOtherCounter), 200 * 3 + 7);
        EXPECT_EQ(cached.Computations(), 3U);
    }

    TEST(ValueCachedTest, AContextRebuiltOverTheSameObjectsAndCountersHits) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kIntFormula));
        static_cast<void>(cached.Evaluate(world.Rebuild()));
        for (int frame = 0; frame < 50; ++frame) {
            const Context frameContext = world.Rebuild();
            EXPECT_EQ(cached.Evaluate(frameContext), cached.Source().Evaluate(frameContext));
        }
        EXPECT_EQ(cached.Computations(), 1U);
    }

    TEST(ValueCachedTest, AProgramThatReadsNothingRunsOnceUntilInvalidatedOrReset) {
        const Value<int32_t> constant = HandAssembledConstant();
        ASSERT_EQ(constant.Kind(), ValueKind::Program);
        ASSERT_EQ(constant.Reads(), 0);

        CachedWorld world;
        CachedValue<int32_t> cached(constant);
        for (int i = 0; i < 10; ++i) {
            EXPECT_EQ(cached.Evaluate(world.context), 42);
        }
        EXPECT_EQ(cached.Evaluate(Context()), 42) << "no level is read, so any context will do";
        EXPECT_EQ(cached.Computations(), 1U);

        ++world.casterVersion;
        EXPECT_EQ(cached.Evaluate(world.context), 42);
        EXPECT_EQ(cached.Computations(), 1U);

        cached.Invalidate();
        EXPECT_EQ(cached.Evaluate(world.context), 42);
        EXPECT_EQ(cached.Computations(), 2U);

        cached.Reset(constant);
        EXPECT_EQ(cached.Evaluate(world.context), 42);
        EXPECT_EQ(cached.Computations(), 3U);
    }

    TEST(ValueCachedTest, ALevelWithNoCounterRunsTheProgramOnEveryCallAndSaysSoOnce) {
        ValueWorld world;
        constexpr StringView kUnversionedFormula = "caster.attack * 5 - 11";
        CachedValue<int32_t> cached(Require(CompileValue<int32_t>(kUnversionedFormula, world.declaration)));

        LogRecorder recorder;
        constexpr uint32_t kCalls = 5;
        for (uint32_t i = 0; i < kCalls; ++i) {
            world.caster.attack = static_cast<int32_t>(i);
            EXPECT_EQ(cached.Evaluate(world.context), static_cast<int32_t>(i) * 5 - 11);
            EXPECT_EQ(cached.Computations(), i + 1);
        }
        LogEvent::Flush();
        const size_t warnings = recorder.Count(LogLevel::Warning, {"CachedValue", kUnversionedFormula});
#if ENGINE_ENABLE_ASSERTS
        EXPECT_EQ(warnings, 1U);
#else
        EXPECT_EQ(warnings, 0U);
#endif
    }

    TEST(ValueCachedTest, OneUnversionedLevelOfTwoStopsTheCache) {
        CachedWorld versioned;
        ValueCastInfo cast;
        Context partly(versioned.declaration);
        partly.Set(kCasterLevel, versioned.caster, &versioned.casterVersion);
        partly.Set(kCastLevel, cast);
        CachedValue<int32_t> cached(versioned.Compile<int32_t>(kIntFormula));
        EXPECT_EQ(cached.Evaluate(partly), 12 * (100 + 3 * 10) / 100);
        cast.level = 9;
        EXPECT_EQ(cached.Evaluate(partly), 12 * (100 + 9 * 10) / 100) << "a write to the level that has no counter is seen";
        EXPECT_EQ(cached.Computations(), 2U);
    }

    TEST(ValueCachedTest, ResetTakesAnotherFormulaAndInvalidateKeepsTheSame) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kCasterOnlyFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);
        cached.Reset(world.Compile<int32_t>(kIntFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * (100 + 3 * 10) / 100);
        EXPECT_EQ(cached.Computations(), 2U);

        cached.Invalidate();
        EXPECT_EQ(cached.Evaluate(world.context), 12 * (100 + 3 * 10) / 100);
        EXPECT_EQ(cached.Computations(), 3U);

        cached.Reset(Value<int32_t>(7));
        EXPECT_EQ(cached.Evaluate(world.context), 7);
    }

    TEST(ValueCachedTest, AResetToAFormulaOverOtherLevelsWatchesTheNewLevels) {
        CachedWorld world;
        CachedValue<int32_t> cached(world.Compile<int32_t>(kCasterOnlyFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);

        cached.Reset(world.Compile<int32_t>(kIntFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * (100 + 3 * 10) / 100);
        ++world.castVersion;
        world.cast.level = 5;
        EXPECT_EQ(cached.Evaluate(world.context), 12 * (100 + 5 * 10) / 100) << "the second level is read now";
        EXPECT_EQ(cached.Computations(), 3U);

        cached.Reset(world.Compile<int32_t>(kCasterOnlyFormula));
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);
        ++world.castVersion;
        EXPECT_EQ(cached.Evaluate(world.context), 12 * 3 + 7);
        EXPECT_EQ(cached.Computations(), 4U) << "and no longer";
    }

    /// The cache against `Value::Evaluate`, one context rebuilt per step over objects that change and are bumped now and then
    template <typename T>
    void ExpectSameAsEvaluate(StringView formula, void (*change)(ValueUnitStats&, ValueCastInfo&, uint32_t)) {
        CachedWorld world;
        const Value<T> value = world.Compile<T>(formula);
        CachedValue<T> cached(value);
        constexpr uint32_t kSteps = 300;
        constexpr uint32_t kChangeEvery = 7;
        uint32_t expectedComputations = 1;
        for (uint32_t step = 0; step < kSteps; ++step) {
            if (step % kChangeEvery == 0 && step != 0) {
                change(world.caster, world.cast, step);
                ++world.casterVersion;
                ++expectedComputations;
            }
            const Context frame = world.Rebuild();
            EXPECT_EQ(cached.Evaluate(frame), value.Evaluate(frame)) << String(formula).c_str() << " at step " << step;
        }
        EXPECT_EQ(cached.Computations(), expectedComputations) << String(formula).c_str();
    }

    TEST(ValueCachedTest, TheResultIsWhatEvaluateGivesOverManyContexts) {
        ExpectSameAsEvaluate<int32_t>(kIntFormula, [](ValueUnitStats& caster, ValueCastInfo& cast, uint32_t step) {
            caster.attack = static_cast<int32_t>(step) * 3;
            cast.level = static_cast<int32_t>(step % 11);
        });
        ExpectSameAsEvaluate<float>(kFloatFormula, [](ValueUnitStats& caster, ValueCastInfo& cast, uint32_t step) {
            caster.speed = static_cast<float>(step) * 0.25f;
            cast.power = static_cast<float>(step % 5) - 1.5f;
        });
        ExpectSameAsEvaluate<Vec3>(kVec3Formula, [](ValueUnitStats& caster, ValueCastInfo& cast, uint32_t step) {
            caster.velocity = Vec3{static_cast<float>(step), 2.0f, -1.0f};
            cast.target = Vec3{0.5f, static_cast<float>(step % 3), 4.0f};
            cast.power = static_cast<float>(step % 4);
        });
    }

    TEST(ValueCachedTest, ASteadyFrameComputesAndAllocatesNothing) {
        CachedWorld world;
        CachedValue<Vec3> cached(world.Compile<Vec3>(kVec3Formula));
        const Vec3 first = cached.Evaluate(world.context);
        ASSERT_EQ(cached.Computations(), 1U);

        const uint64_t allocationsBefore = Allocations();
        constexpr int kCalls = 1000;
        Vec3 last = first;
        for (int i = 0; i < kCalls; ++i) {
            last = cached.Evaluate(world.context);
        }
        EXPECT_EQ(last, first);
        EXPECT_EQ(cached.Computations(), 1U);
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(Allocations(), allocationsBefore);
        }
    }

}  // namespace BECore::Tests
