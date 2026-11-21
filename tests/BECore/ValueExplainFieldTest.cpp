// The reverse of LinkField (Values/Explain/FieldResolver.h): from a level and an offset the path that links to that place, so a trace can say what a LoadSlot read.

#include <BECore/ProgramListTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Explain/FieldResolver.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ProgramListData;
    using namespace ValueTestData;

    namespace {

        struct Levels {
            ContextDecl declaration;

            Levels() {
                EXPECT_TRUE(declaration.Add("caster", "ValueUnitStats").has_value());
                EXPECT_TRUE(declaration.Add("hero", "ValueHeroStats").has_value());
                EXPECT_TRUE(declaration.Add("widths", "ValueEnumWidths").has_value());
                EXPECT_TRUE(declaration.Add("refused", "ProgramListRefused").has_value());
            }
        };

        /// `path` links to a place and the place resolves back to `path`, with the type the link found
        void ExpectRoundTrip(const Levels& levels, StringView path) {
            const auto linked = LinkField(levels.declaration, path);
            ASSERT_TRUE(linked.has_value()) << String(path).c_str();
            const auto resolved = ResolveField(levels.declaration, linked->slot.level, linked->slot.offset);
            ASSERT_TRUE(resolved.has_value()) << String(path).c_str();
            EXPECT_EQ(resolved->path, String(path));
            ASSERT_NE(resolved->type, nullptr);
            EXPECT_TRUE(SameType(*resolved->type, *linked->type)) << String(path).c_str();
        }

    }  // namespace

    TEST(ValueExplainFieldTest, EveryFieldOfAnObjectResolvesBackToItsPath) {
        const Levels levels;
        for (const StringView field :
             {"alive", "attack", "count", "score", "speed", "precise", "title", "element", "position", "cell", "velocity", "plane", "facing", "bounds", "tint", "spawn", "buffs"}) {
            ExpectRoundTrip(levels, Format("caster.{}", field));
        }
    }

    TEST(ValueExplainFieldTest, AFieldOfAnEmbeddedObjectIsNamedThroughItsOwner) {
        const Levels levels;
        ExpectRoundTrip(levels, "caster.span.low");
        ExpectRoundTrip(levels, "caster.span.high");
        ExpectRoundTrip(levels, "caster.span.offset");
        ExpectRoundTrip(levels, "refused.set.tiles");
    }

    TEST(ValueExplainFieldTest, AnObjectAndItsFirstFieldShareAnOffsetAndTheFieldWins) {
        const Levels levels;
        const auto span = LinkField(levels.declaration, "caster.span.low");
        ASSERT_TRUE(span.has_value());
        // `span` begins where `span.low` does: the leaf is what a LoadSlot reads
        const auto resolved = ResolveField(levels.declaration, span->slot.level, span->slot.offset);
        ASSERT_TRUE(resolved.has_value());
        EXPECT_EQ(resolved->path, String("caster.span.low"));
    }

    TEST(ValueExplainFieldTest, AParentsFieldIsTheDerivedLevelsOwn) {
        const Levels levels;
        ExpectRoundTrip(levels, "hero.attack");
        ExpectRoundTrip(levels, "hero.renown");
        ExpectRoundTrip(levels, "hero.span.high");
    }

    TEST(ValueExplainFieldTest, AFieldOfAnEnumTypeCarriesItsNames) {
        const Levels levels;
        ExpectRoundTrip(levels, "widths.narrow");
        ExpectRoundTrip(levels, "widths.wide16");
        ExpectRoundTrip(levels, "widths.wide32");
        const auto linked = LinkField(levels.declaration, "caster.element");
        ASSERT_TRUE(linked.has_value());
        const auto resolved = ResolveField(levels.declaration, linked->slot.level, linked->slot.offset);
        ASSERT_TRUE(resolved.has_value());
        ASSERT_NE(resolved->type, nullptr);
        EXPECT_EQ(resolved->type->kind, TypeKind::Enum);
        ASSERT_EQ(resolved->type->names.size(), 3U);
        EXPECT_EQ(resolved->type->names[1], StringView("Water"));
    }

    TEST(ValueExplainFieldTest, AnOffsetThatIsNoFieldGivesNoPath) {
        const Levels levels;
        const auto velocity = LinkField(levels.declaration, "caster.velocity");
        ASSERT_TRUE(velocity.has_value());
        // Inside a component, and not at a component's start
        EXPECT_FALSE(ResolveField(levels.declaration, velocity->slot.level, velocity->slot.offset + 1).has_value());
        EXPECT_FALSE(ResolveField(levels.declaration, 0, 1U << 20).has_value());
        EXPECT_FALSE(ResolveField(levels.declaration, static_cast<LevelIndex>(levels.declaration.Size()), 0).has_value());
    }

    TEST(ValueExplainFieldTest, AComponentOfAMathFieldIsNamedAfterTheField) {
        const Levels levels;
        const auto velocity = LinkField(levels.declaration, "caster.velocity");
        ASSERT_TRUE(velocity.has_value());
        const LevelIndex level = velocity->slot.level;
        const uint32_t offset = velocity->slot.offset;
        const auto y = ResolveField(levels.declaration, level, offset + sizeof(float));
        ASSERT_TRUE(y.has_value());
        EXPECT_EQ(y->path, String("caster.velocity.y"));
        ASSERT_NE(y->type, nullptr);
        EXPECT_EQ(y->type->kind, TypeKind::Float);
        // At the field's own offset the type read decides: the vector is the field, a float is its first component
        EXPECT_EQ(ResolveField(levels.declaration, level, offset)->path, String("caster.velocity"));
        EXPECT_EQ(ResolveField(levels.declaration, level, offset, ProgramType::Vec3)->path, String("caster.velocity"));
        EXPECT_EQ(ResolveField(levels.declaration, level, offset, ProgramType::Float)->path, String("caster.velocity.x"));
        // A colour's channels are bytes read through `Extract`: no field of their own
        const auto tint = LinkField(levels.declaration, "caster.tint");
        ASSERT_TRUE(tint.has_value());
        EXPECT_FALSE(ResolveField(levels.declaration, tint->slot.level, tint->slot.offset + 1).has_value());
    }

    TEST(ValueExplainFieldTest, TheFieldOfAListElementIsNamedRelativeToTheElement) {
        TypeRegistry& registry = TypeRegistry::GetInstance();
        const TypeInfo* tile = registry.Find("ProgramListTile");
        const TypeInfo* crate = registry.Find("ProgramListCrate");
        ASSERT_NE(tile, nullptr);
        ASSERT_NE(crate, nullptr);
        for (const StringView field : {"suit", "terminal", "rank", "weight", "position", "name"}) {
            const auto linked = LinkElementField(*tile, field);
            ASSERT_TRUE(linked.has_value()) << String(field).c_str();
            const auto resolved = ResolveElementField(*tile, linked->offset);
            ASSERT_TRUE(resolved.has_value()) << String(field).c_str();
            EXPECT_EQ(resolved->path, String(field));
        }
        const auto nested = LinkElementField(*crate, "tile.rank");
        ASSERT_TRUE(nested.has_value());
        const auto resolved = ResolveElementField(*crate, nested->offset);
        ASSERT_TRUE(resolved.has_value());
        EXPECT_EQ(resolved->path, String("tile.rank"));
        EXPECT_FALSE(ResolveElementField(*tile, 1U << 20).has_value());
    }

}  // namespace BECore::Tests
