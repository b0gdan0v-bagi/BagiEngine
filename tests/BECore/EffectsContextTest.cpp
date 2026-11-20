// The parts of a context a script of Values/Effects needs: a writable level, a field linked with its own type and the write targets of a declaration.

#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Effects/WriteTargets.h>
#include <BECore/Values/Value.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <cstring>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;

        struct TwoLevels {
            ContextDecl declaration;

            TwoLevels() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
            }
        };

        /// The refusal of both entry points for one path must be the same code and the same text
        void ExpectSameRefusal(const ContextDecl& declaration, StringView path, LinkErrorCode code) {
            const auto field = LinkField(declaration, path);
            const auto slot = LinkSlot(declaration, path, DescribeType<int32_t>());
            ASSERT_FALSE(field.has_value()) << String(path).c_str();
            ASSERT_FALSE(slot.has_value()) << String(path).c_str();
            EXPECT_EQ(field.error().code, code) << field.error().message.c_str();
            EXPECT_EQ(slot.error().code, code) << slot.error().message.c_str();
            EXPECT_EQ(field.error().message, slot.error().message);
        }

    }  // namespace

    TEST(EffectsContextTest, AMutableLevelIsWrittenThroughAndReadBack) {
        const TwoLevels levels;
        ValueUnitStats caster;
        const ValueCastInfo cast;
        Context context(levels.declaration);
        context.SetMutable(kCasterLevel, caster);
        context.Set(kCastLevel, cast);
        EXPECT_TRUE(context.IsMutable(kCasterLevel));
        EXPECT_FALSE(context.IsMutable(kCastLevel));
        EXPECT_EQ(context.MutableObject(kCastLevel), nullptr);

        const auto slot = LinkSlot(levels.declaration, "caster.attack", DescribeType<int32_t>());
        ASSERT_TRUE(slot.has_value());
        void* object = context.MutableObject(slot->level);
        ASSERT_NE(object, nullptr);
        const int32_t stored = 321;
        std::memcpy(static_cast<char*>(object) + slot->offset, &stored, sizeof(stored));
        EXPECT_EQ(caster.attack, 321);

        Value<int32_t> attack;
        ASSERT_TRUE(attack.Link(levels.declaration, "caster.attack").has_value());
        EXPECT_EQ(attack.Evaluate(context), 321);
    }

    TEST(EffectsContextTest, ALaterSetClearsTheMark) {
        ValueUnitStats caster;
        Context context;
        context.SetMutable(kCasterLevel, caster);
        ASSERT_TRUE(context.IsMutable(kCasterLevel));
        context.Set(kCasterLevel, caster);
        EXPECT_FALSE(context.IsMutable(kCasterLevel));
        EXPECT_EQ(context.MutableObject(kCasterLevel), nullptr);
        EXPECT_EQ(context.Object(kCasterLevel), &caster);
    }

    TEST(EffectsContextTest, TheCounterOfAMutableLevelBumpsAndMovesTheKey) {
        ValueUnitStats caster;
        uint64_t version = 5;
        Context context;
        context.SetMutable(kCasterLevel, caster, &version);
        const uint64_t before = context.VersionKey(LevelBit(kCasterLevel));
        context.BumpVersion(kCasterLevel);
        EXPECT_EQ(version, 6U);
        EXPECT_EQ(context.Version(kCasterLevel), 6U);
        EXPECT_NE(context.VersionKey(LevelBit(kCasterLevel)), before);
    }

    TEST(EffectsContextTest, ALevelWithoutACounterOrNotMutableBumpsNothing) {
        ValueUnitStats caster;
        ValueCastInfo cast;
        uint64_t castVersion = 1;
        Context context;
        context.SetMutable(kCasterLevel, caster);
        context.Set(kCastLevel, cast, &castVersion);
        context.BumpVersion(kCasterLevel);
        context.BumpVersion(kCastLevel);
        EXPECT_EQ(context.Version(kCasterLevel), 0U);
        EXPECT_EQ(castVersion, 1U);
    }

    TEST(EffectsContextTest, SetMutableLeavesTheSetCountsAndKeysOfSetAlone) {
        ValueUnitStats caster;
        uint64_t version = 2;
        Context viaSet;
        Context viaMutable;
        viaSet.Set(kCasterLevel, caster, &version);
        viaMutable.SetMutable(kCasterLevel, caster, &version);
        EXPECT_EQ(viaSet.VersionKey(LevelBit(kCasterLevel)), viaMutable.VersionKey(LevelBit(kCasterLevel)));
        EXPECT_TRUE(viaMutable.IsVersioned(LevelBit(kCasterLevel)));
    }

    TEST(EffectsContextTest, AnObjectOfADerivedTypeStaysWritableAtItsBaseSubobject) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ValueHeroStats hero;
        Context context(declaration);
        context.SetMutable(kCasterLevel, hero);
        ASSERT_TRUE(context.IsMutable(kCasterLevel));

        const auto slot = LinkSlot(declaration, "caster.attack", DescribeType<int32_t>());
        ASSERT_TRUE(slot.has_value());
        const int32_t stored = 654;
        std::memcpy(static_cast<char*>(context.MutableObject(kCasterLevel)) + slot->offset, &stored, sizeof(stored));
        EXPECT_EQ(hero.attack, 654);
        EXPECT_EQ(context.Object(kCasterLevel), static_cast<const ValueUnitStats*>(&hero));
    }

    TEST(EffectsContextTest, ALinkedFieldHasTheSlotOfLinkSlotAndItsOwnType) {
        const TwoLevels levels;
        const auto field = LinkField(levels.declaration, "caster.velocity");
        ASSERT_TRUE(field.has_value());
        const auto slot = LinkSlot(levels.declaration, "caster.velocity", DescribeType<Vec3>());
        ASSERT_TRUE(slot.has_value());
        EXPECT_EQ(field->slot.level, slot->level);
        EXPECT_EQ(field->slot.offset, slot->offset);
        EXPECT_TRUE(SameType(Require(field->type), DescribeType<Vec3>()));

        const auto element = LinkField(levels.declaration, "caster.element");
        ASSERT_TRUE(element.has_value());
        EXPECT_EQ(Require(element->type).kind, TypeKind::Enum);
    }

    TEST(EffectsContextTest, EveryRefusedPathHasTheMessageOfLinkSlot) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        ASSERT_TRUE(declaration.Add<ValueBehind>("behind").has_value());
        ExpectSameRefusal(declaration, "casster.attack", LinkErrorCode::unknown_level);
        ExpectSameRefusal(declaration, "caster.attak", LinkErrorCode::unknown_field);
        ExpectSameRefusal(declaration, "caster.span.hight", LinkErrorCode::unknown_field);
        ExpectSameRefusal(declaration, "caster.position.x", LinkErrorCode::unknown_field);
        ExpectSameRefusal(declaration, "behind.spans[1].low", LinkErrorCode::not_fixed);
        ExpectSameRefusal(declaration, "caster.buffs[0]", LinkErrorCode::not_fixed);
        ExpectSameRefusal(declaration, "caster", LinkErrorCode::invalid_path);
        ExpectSameRefusal(declaration, "caster[0].attack", LinkErrorCode::invalid_path);
        ExpectSameRefusal(declaration, "caster..attack", LinkErrorCode::invalid_path);
    }

    TEST(EffectsContextTest, AFieldOfAnyTypeLinksWhereLinkSlotWouldMismatch) {
        const TwoLevels levels;
        EXPECT_FALSE(LinkSlot(levels.declaration, "caster.speed", DescribeType<int32_t>()).has_value());
        EXPECT_TRUE(LinkField(levels.declaration, "caster.speed").has_value());
    }

    TEST(EffectsContextTest, WriteTargetsPermitAPathAndWhatIsUnderIt) {
        WriteTargets targets;
        EXPECT_TRUE(targets.Allow("cell.timer"));
        EXPECT_TRUE(targets.Allow("board"));
        EXPECT_TRUE(targets.Permits("cell.timer"));
        EXPECT_TRUE(targets.Permits("board"));
        EXPECT_TRUE(targets.Permits("board.mask"));
        EXPECT_TRUE(targets.Permits("board.stats.count"));
        EXPECT_FALSE(targets.Permits("cell"));
        EXPECT_FALSE(targets.Permits("cell.open"));
        EXPECT_FALSE(targets.Permits("cell.timers"));
        EXPECT_FALSE(targets.Permits("boards"));
        EXPECT_FALSE(targets.Permits("boar"));
        EXPECT_FALSE(targets.Permits(""));
    }

    TEST(EffectsContextTest, ALevelPrefixStopsAtASegmentBoundary) {
        WriteTargets targets;
        ASSERT_TRUE(targets.Allow("cell.t"));
        EXPECT_TRUE(targets.Permits("cell.t"));
        EXPECT_FALSE(targets.Permits("cell.timer"));
        ASSERT_TRUE(targets.Allow("cell"));
        EXPECT_TRUE(targets.Permits("cell.timer"));
    }

    TEST(EffectsContextTest, AnEmptySetPermitsNothing) {
        const WriteTargets targets;
        EXPECT_TRUE(targets.Empty());
        EXPECT_FALSE(targets.Permits("cell"));
        EXPECT_FALSE(targets.Permits("cell.timer"));
        EXPECT_TRUE(targets.Paths().empty());
    }

    TEST(EffectsContextTest, APathThatIsNoDottedNameIsNotAllowed) {
        WriteTargets targets;
        for (const StringView bad : {"", ".", "cell.", ".cell", "cell..timer", "cell[0]", "cell.timer ", "1cell", "cell-timer", "cell.1"}) {
            EXPECT_FALSE(targets.Allow(bad)) << String(bad).c_str();
        }
        EXPECT_TRUE(targets.Empty());
    }

    TEST(EffectsContextTest, PathsListsEachAllowedPathOnceInOrder) {
        WriteTargets targets;
        ASSERT_TRUE(targets.Allow("cell.timer"));
        ASSERT_TRUE(targets.Allow("board"));
        ASSERT_TRUE(targets.Allow("cell.timer"));
        const Span<const String> paths = targets.Paths();
        ASSERT_EQ(paths.size(), 2U);
        EXPECT_EQ(paths[0], "cell.timer");
        EXPECT_EQ(paths[1], "board");
    }

}  // namespace BECore::Tests
