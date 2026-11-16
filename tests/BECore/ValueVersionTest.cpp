// The versions of the levels of a Context (Values/Context.h) and the levels a set of values reads (Value::Reads, ValueDependencies): what
// VersionKey promises to a screen that rebuilds only when something it reads has changed. The context types are those of ValueTestTypes.h.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
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

}  // namespace BECore::Tests
