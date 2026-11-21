// The versions of the levels of a Context (Values/Context.h) and the levels a set of values reads (Value::Reads, ValueDependencies): what
// VersionKey promises to a screen that rebuilds only when something it reads has changed. The context types are those of ValueTestTypes.h.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <bit>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;

        /// The two levels of `ValueWorld`, each with a counter its owner bumps, and two values: one reads `caster`, one reads `cast`
        struct VersionedWorld {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            uint64_t casterVersion = 1;
            uint64_t castVersion = 1;
            Context context;
            Value<float> speed;
            Value<int32_t> level;

            VersionedWorld() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                context = Context(declaration);
                context.Set(kCasterLevel, caster, &casterVersion);
                context.Set(kCastLevel, cast, &castVersion);
                EXPECT_TRUE(speed.Link(declaration, "caster.speed").has_value());
                EXPECT_TRUE(level.Link(declaration, "cast.level").has_value());
            }

            VersionedWorld(const VersionedWorld&) = delete;
            VersionedWorld& operator=(const VersionedWorld&) = delete;

            [[nodiscard]] LevelMask ReadsOf(bool caster, bool cast) const {
                ValueDependencies dependencies;
                if (caster) {
                    dependencies.Add(speed);
                }
                if (cast) {
                    dependencies.Add(level);
                }
                return dependencies.Mask();
            }
        };

    }  // namespace

    static_assert(LevelBit(0) == 0b1 && LevelBit(3) == 0b1000 && LevelBit(7) == 0b10000000);
    static_assert(Value<float>{}.Reads() == 0);
    static_assert(Value<float>::Const(2.0f).Reads() == 0);
    static_assert(sizeof(Value<float>) == 24, "Get and the layout of Value are untouched by the versions");

    TEST(ValueVersionTest, CompileTimeChecks) {}

    TEST(ValueVersionTest, ASlotReadsTheBitOfItsLevelAndAConstReadsNone) {
        VersionedWorld world;
        EXPECT_EQ(world.speed.Reads(), LevelBit(kCasterLevel));
        EXPECT_EQ(world.level.Reads(), LevelBit(kCastLevel));
        EXPECT_EQ(Value<int32_t>(5).Reads(), 0);
        EXPECT_EQ(Value<int32_t>().Reads(), 0);
    }

    TEST(ValueVersionTest, TheMaskOfASetOfValuesIsTheUnionOfWhatTheySlotRead) {
        VersionedWorld world;
        EXPECT_EQ(world.ReadsOf(false, false), 0);
        EXPECT_EQ(world.ReadsOf(true, false), LevelBit(kCasterLevel));
        EXPECT_EQ(world.ReadsOf(true, true), LevelBit(kCasterLevel) | LevelBit(kCastLevel));

        ValueDependencies dependencies;
        dependencies.Add(Value<Vec3>(Vec3{1.0f, 2.0f, 3.0f})).Add(world.level);
        EXPECT_EQ(dependencies.Mask(), LevelBit(kCastLevel));
    }

    TEST(ValueVersionTest, ALevelReportsItsCounterAndZeroWithNone) {
        VersionedWorld world;
        EXPECT_EQ(world.context.Version(kCasterLevel), 1U);
        world.casterVersion = 41;
        EXPECT_EQ(world.context.Version(kCasterLevel), 41U);
        EXPECT_EQ(world.context.Version(kCastLevel), 1U);

        Context bare;
        EXPECT_EQ(bare.Version(kCasterLevel), 0U);
        bare.Set(kCasterLevel, world.caster);
        EXPECT_EQ(bare.Version(kCasterLevel), 0U);
    }

    TEST(ValueVersionTest, TheKeyIsStableWhileNothingChanges) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        const uint64_t key = world.context.VersionKey(both);
        EXPECT_EQ(world.context.VersionKey(both), key);
        world.caster.speed = 99.0f;
        EXPECT_EQ(world.context.VersionKey(both), key) << "a plain write is not a version: the owner bumps the counter";
    }

    TEST(ValueVersionTest, TheKeyIgnoresALevelTheValuesDoNotRead) {
        VersionedWorld world;
        const LevelMask casterOnly = world.ReadsOf(true, false);
        const uint64_t key = world.context.VersionKey(casterOnly);
        ++world.castVersion;
        EXPECT_EQ(world.context.VersionKey(casterOnly), key);
        world.context.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_EQ(world.context.VersionKey(casterOnly), key) << "a Set of a level that is not read does not move the key either";
    }

    TEST(ValueVersionTest, TheKeyChangesWhenEitherReadLevelIsBumped) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        const uint64_t before = world.context.VersionKey(both);

        ++world.casterVersion;
        const uint64_t afterCaster = world.context.VersionKey(both);
        EXPECT_NE(afterCaster, before);

        ++world.castVersion;
        const uint64_t afterBoth = world.context.VersionKey(both);
        EXPECT_NE(afterBoth, afterCaster);
        EXPECT_NE(afterBoth, before);
    }

    TEST(ValueVersionTest, EveryBumpGivesAKeyNeverSeenBefore) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        HashSet<uint64_t> seen;
        constexpr uint32_t kBumps = 2000;
        for (uint32_t i = 0; i < kBumps; ++i) {
            EXPECT_TRUE(seen.insert(world.context.VersionKey(both)).second) << i;
            ++((i % 3 == 0) ? world.castVersion : world.casterVersion);
        }
    }

    TEST(ValueVersionTest, TheKeyChangesWhenAReadLevelIsGivenAnotherObject) {
        VersionedWorld world;
        const LevelMask casterOnly = world.ReadsOf(true, false);
        const uint64_t key = world.context.VersionKey(casterOnly);
        ValueUnitStats other;
        world.context.Set(kCasterLevel, other, &world.casterVersion);
        EXPECT_NE(world.context.VersionKey(casterOnly), key);
        EXPECT_EQ(world.context.Object(kCasterLevel), &other);
    }

    TEST(ValueVersionTest, TheKeyChangesWhenAReadLevelIsGivenAnotherCounterOfTheSameNumber) {
        VersionedWorld world;
        const LevelMask casterOnly = world.ReadsOf(true, false);
        const uint64_t key = world.context.VersionKey(casterOnly);
        uint64_t otherCounter = world.casterVersion;
        world.context.Set(kCasterLevel, world.caster, &otherCounter);
        EXPECT_NE(world.context.VersionKey(casterOnly), key);
    }

    TEST(ValueVersionTest, ASetWithNoCounterDropsTheOldOne) {
        VersionedWorld world;
        world.context.Set(kCasterLevel, world.caster);
        EXPECT_EQ(world.context.Version(kCasterLevel), 0U);
        const uint64_t key = world.context.VersionKey(LevelBit(kCasterLevel));
        ++world.casterVersion;
        EXPECT_EQ(world.context.VersionKey(LevelBit(kCasterLevel)), key);
    }

    TEST(ValueVersionTest, ALevelWithNoCounterNeverMovesTheKeyByItself) {
        ValueUnitStats caster;
        Context context;
        context.Set(kCasterLevel, caster);
        const uint64_t key = context.VersionKey(LevelBit(kCasterLevel));
        caster.attack = 500;
        EXPECT_EQ(context.VersionKey(LevelBit(kCasterLevel)), key);
    }

    TEST(ValueVersionTest, ANoLevelMaskHasAKeyThatNothingMoves) {
        VersionedWorld world;
        const uint64_t key = world.context.VersionKey(0);
        ++world.casterVersion;
        ++world.castVersion;
        world.context.Set(kCasterLevel, world.caster, &world.casterVersion);
        EXPECT_EQ(world.context.VersionKey(0), key);
        EXPECT_EQ(Context().VersionKey(0), key);
    }

    TEST(ValueVersionTest, TheLevelsKeepTheirPlaceInTheKey) {
        VersionedWorld world;
        // Same counter value at both levels: the key of one level must not equal the key of the other
        EXPECT_NE(world.context.VersionKey(LevelBit(kCasterLevel)), world.context.VersionKey(LevelBit(kCastLevel)));
        EXPECT_NE(world.context.VersionKey(LevelBit(kCasterLevel) | LevelBit(kCastLevel)), world.context.VersionKey(LevelBit(kCasterLevel)));
    }

    TEST(ValueVersionTest, ADerivedObjectAtABaseLevelKeepsItsCounter) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ValueHeroStats hero;
        uint64_t version = 7;
        Context context(declaration);
        context.Set(kCasterLevel, hero, &version);
        EXPECT_EQ(context.Version(kCasterLevel), 7U);
        const uint64_t key = context.VersionKey(LevelBit(kCasterLevel));
        ++version;
        EXPECT_NE(context.VersionKey(LevelBit(kCasterLevel)), key);
    }

    TEST(ValueVersionTest, ARebuiltContextOverOtherObjectsRepeatsNoKey) {
        constexpr size_t kObjects = 64;
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        Vector<ValueUnitStats> objects(kObjects);
        uint64_t version = 1;
        HashSet<uint64_t> seen;
        for (ValueUnitStats& object : objects) {
            // A fresh context each time: every Set count is 1 and the counter holds the same number
            Context context(declaration);
            context.Set(kCasterLevel, object, &version);
            EXPECT_TRUE(seen.insert(context.VersionKey(LevelBit(kCasterLevel))).second);
        }
    }

    TEST(ValueVersionTest, ARebuiltContextOverTheSameObjectsAndCountersGivesTheSameKey) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        const uint64_t key = world.context.VersionKey(both);
        for (int frame = 0; frame < 3; ++frame) {
            Context rebuilt(world.declaration);
            rebuilt.Set(kCasterLevel, world.caster, &world.casterVersion);
            rebuilt.Set(kCastLevel, world.cast, &world.castVersion);
            EXPECT_EQ(rebuilt.VersionKey(both), key);
        }
        Context twice(world.declaration);
        twice.Set(kCasterLevel, world.caster, &world.casterVersion);
        twice.Set(kCasterLevel, world.caster, &world.casterVersion);
        twice.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_NE(twice.VersionKey(both), key) << "the count of Set calls is still part of the key";
    }

    TEST(ValueVersionTest, AnotherCounterObjectOfTheSameValueMovesTheKeyOfARebuiltContext) {
        VersionedWorld world;
        const LevelMask casterOnly = world.ReadsOf(true, false);
        uint64_t otherCounter = world.casterVersion;
        Context rebuilt(world.declaration);
        rebuilt.Set(kCasterLevel, world.caster, &otherCounter);
        EXPECT_NE(rebuilt.VersionKey(casterOnly), world.context.VersionKey(casterOnly));
    }

    TEST(ValueVersionTest, ALevelWithNoObjectOrNoCounterFoldsAConstant) {
        ValueUnitStats caster;
        uint64_t version = 3;
        Context none;
        Context noCounter;
        noCounter.Set(kCasterLevel, caster);
        const LevelMask mask = LevelBit(kCasterLevel);
        EXPECT_EQ(none.VersionKey(mask), Context().VersionKey(mask));
        EXPECT_NE(none.VersionKey(mask), noCounter.VersionKey(mask)) << "an object at the level is part of the key even with no counter";
        ++version;
        Context counterOnly;
        counterOnly.Set(kCasterLevel, caster, &version);
        EXPECT_NE(noCounter.VersionKey(mask), counterOnly.VersionKey(mask));
    }

    TEST(ValueVersionTest, IsVersionedNeedsAnObjectAndACounterAtEveryLevelOfTheMask) {
        ValueUnitStats caster;
        ValueCastInfo cast;
        uint64_t version = 1;
        Context context;
        const LevelMask casterBit = LevelBit(kCasterLevel);
        const LevelMask castBit = LevelBit(kCastLevel);

        EXPECT_TRUE(context.IsVersioned(0));
        EXPECT_FALSE(context.IsVersioned(casterBit));

        context.Set(kCasterLevel, caster);
        EXPECT_FALSE(context.IsVersioned(casterBit)) << "an object with no counter";
        EXPECT_TRUE(context.IsVersioned(0));

        context.Set(kCasterLevel, caster, &version);
        EXPECT_TRUE(context.IsVersioned(casterBit));
        EXPECT_FALSE(context.IsVersioned(casterBit | castBit)) << "one level of the mask has nothing";
        EXPECT_TRUE(context.IsVersioned(0)) << "levels outside the mask do not matter";

        context.Set(kCastLevel, cast, &version);
        EXPECT_TRUE(context.IsVersioned(casterBit | castBit));

        context.Set(kCasterLevel, caster);
        EXPECT_FALSE(context.IsVersioned(casterBit | castBit)) << "a Set with no counter drops the counter";
        EXPECT_TRUE(context.IsVersioned(castBit));
    }

    namespace {

        /// What `Context::Stamp` wrote for @p mask, in the vector a cache keeps
        [[nodiscard]] Vector<LevelStamp> StampOf(const Context& context, LevelMask mask) {
            Vector<LevelStamp> stamps(static_cast<size_t>(std::popcount(mask)));
            context.Stamp(mask, stamps.data());
            return stamps;
        }

    }  // namespace

    TEST(ValueVersionTest, AStampMatchesWhileNothingChangesAndOverARebuiltContext) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        const Vector<LevelStamp> stamps = StampOf(world.context, both);
        EXPECT_TRUE(world.context.Matches(both, stamps.data()));
        world.caster.speed = 99.0f;
        EXPECT_TRUE(world.context.Matches(both, stamps.data())) << "a plain write is not a version";

        world.context.Set(kCastLevel, world.cast, &world.castVersion);
        const Vector<LevelStamp> afterSet = StampOf(world.context, both);
        Context nextFrame(world.declaration);
        nextFrame.Set(kCasterLevel, world.caster, &world.casterVersion);
        nextFrame.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_FALSE(nextFrame.Matches(both, afterSet.data())) << "a context whose level was Set twice has a Set count of 2";
        const Vector<LevelStamp> frame = StampOf(nextFrame, both);
        Context sameFrameAgain(world.declaration);
        sameFrameAgain.Set(kCasterLevel, world.caster, &world.casterVersion);
        sameFrameAgain.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_TRUE(sameFrameAgain.Matches(both, frame.data())) << "a rebuilt context matches: its Set counts restart in each";
    }

    TEST(ValueVersionTest, AStampFailsAfterAChangeOfALevelItWasTakenOver) {
        VersionedWorld world;
        const LevelMask both = world.ReadsOf(true, true);
        const Vector<LevelStamp> stamps = StampOf(world.context, both);
        ++world.castVersion;
        EXPECT_FALSE(world.context.Matches(both, stamps.data())) << "a bump of the second level";

        VersionedWorld bumped;
        const Vector<LevelStamp> bumpedStamps = StampOf(bumped.context, both);
        ++bumped.casterVersion;
        EXPECT_FALSE(bumped.context.Matches(both, bumpedStamps.data())) << "a bump of the first level";

        VersionedWorld setAgain;
        const Vector<LevelStamp> setStamps = StampOf(setAgain.context, both);
        setAgain.context.Set(kCastLevel, setAgain.cast, &setAgain.castVersion);
        EXPECT_FALSE(setAgain.context.Matches(both, setStamps.data())) << "a Set of a read level counts, even over the same object";

        VersionedWorld otherObject;
        const Vector<LevelStamp> objectStamps = StampOf(otherObject.context, both);
        ValueUnitStats other;
        otherObject.context.Set(kCasterLevel, other, &otherObject.casterVersion);
        EXPECT_FALSE(otherObject.context.Matches(both, objectStamps.data()));

        VersionedWorld otherCounter;
        const Vector<LevelStamp> counterStamps = StampOf(otherCounter.context, both);
        uint64_t sameNumber = otherCounter.castVersion;
        otherCounter.context.Set(kCastLevel, otherCounter.cast, &sameNumber);
        EXPECT_FALSE(otherCounter.context.Matches(both, counterStamps.data())) << "another counter object with the same value";

        VersionedWorld noCounter;
        const Vector<LevelStamp> withCounterStamps = StampOf(noCounter.context, both);
        noCounter.context.Set(kCastLevel, noCounter.cast);
        EXPECT_FALSE(noCounter.context.Matches(both, withCounterStamps.data())) << "a level that lost its counter";
    }

    TEST(ValueVersionTest, AStampIgnoresALevelOutsideItsMask) {
        VersionedWorld world;
        const LevelMask casterOnly = world.ReadsOf(true, false);
        const Vector<LevelStamp> stamps = StampOf(world.context, casterOnly);
        ASSERT_EQ(stamps.size(), 1U);
        ++world.castVersion;
        world.context.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_TRUE(world.context.Matches(casterOnly, stamps.data()));
        EXPECT_TRUE(world.context.Matches(0, nullptr)) << "the empty mask has no level to differ";
    }

    TEST(ValueVersionTest, AStampMatchesExactlyWhenTheKeyIsTheSameOverRandomChanges) {
        VersionedWorld world;
        ValueUnitStats otherCaster;
        uint64_t otherCasterVersion = 1;
        const LevelMask masks[] = {world.ReadsOf(true, false), world.ReadsOf(false, true), world.ReadsOf(true, true)};
        constexpr size_t kMasks = std::size(masks);
        Vector<LevelStamp> stamps[kMasks];
        uint64_t keys[kMasks];
        const auto retake = [&](size_t i) {
            stamps[i] = StampOf(world.context, masks[i]);
            keys[i] = world.context.VersionKey(masks[i]);
        };
        for (size_t i = 0; i < kMasks; ++i) {
            retake(i);
        }

        uint32_t seed = 4242;
        const auto next = [&seed](uint32_t bound) {
            seed = seed * 1664525U + 1013904223U;
            return (seed >> 16) % bound;
        };
        constexpr uint32_t kSteps = 3000;
        constexpr uint32_t kEvents = 7;
        constexpr uint32_t kRetakeEvery = 5;
        uint32_t movedChecks = 0;
        for (uint32_t step = 0; step < kSteps; ++step) {
            switch (next(kEvents)) {
                case 0:
                    ++world.casterVersion;
                    break;
                case 1:
                    ++world.castVersion;
                    break;
                case 2:
                    world.context.Set(kCasterLevel, world.caster, &world.casterVersion);
                    break;
                case 3:
                    world.context.Set(kCastLevel, world.cast, &world.castVersion);
                    break;
                case 4:
                    world.context.Set(kCasterLevel, otherCaster, &otherCasterVersion);
                    break;
                case 5:
                    world.context.Set(kCastLevel, world.cast);
                    break;
                default:
                    world.caster.speed += 1.0f;
                    break;
            }
            for (size_t i = 0; i < kMasks; ++i) {
                const bool sameKey = world.context.VersionKey(masks[i]) == keys[i];
                EXPECT_EQ(world.context.Matches(masks[i], stamps[i].data()), sameKey) << "step " << step << " mask " << i;
                movedChecks += sameKey ? 0U : 1U;
                if (next(kRetakeEvery) == 0) {
                    retake(i);
                }
            }
        }
        EXPECT_GT(movedChecks, 0U);
    }

    TEST(ValueVersionTest, ADerivedObjectAtABaseLevelKeysOnTheBaseSubobject) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ValueHeroStats first;
        ValueHeroStats second;
        uint64_t version = 7;
        Context a(declaration);
        a.Set(kCasterLevel, first, &version);
        Context b(declaration);
        b.Set(kCasterLevel, first, &version);
        Context c(declaration);
        c.Set(kCasterLevel, second, &version);
        EXPECT_TRUE(a.IsVersioned(LevelBit(kCasterLevel)));
        EXPECT_EQ(a.VersionKey(LevelBit(kCasterLevel)), b.VersionKey(LevelBit(kCasterLevel)));
        EXPECT_NE(a.VersionKey(LevelBit(kCasterLevel)), c.VersionKey(LevelBit(kCasterLevel)));
    }

}  // namespace BECore::Tests
