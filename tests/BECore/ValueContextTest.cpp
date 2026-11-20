// The context of values (Values/Context.h): the declaration of its levels, the objects behind them and the linking of a path to a slot.
// The generated code of the value test types is included here only; the other value tests find the types by name.

#include <BECore/ProgramListTypes.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Programs/Lists/ListImage.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Generated/ValueTestTypes.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;
    using namespace ProgramListData;

    namespace {

        /// A context of the two levels the tests use: `caster` and `cast`
        struct TwoLevels {
            ContextDecl declaration;

            TwoLevels() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
            }
        };

        /// The slot of `path` finds the very member of the object set in the context
        template <typename T, typename Level, typename Member>
        void ExpectSlotFindsMember(const ContextDecl& declaration, StringView path, LevelIndex index, const Level& object, const Member& member) {
            Context context(declaration);
            context.Set(index, object);
            const auto slot = LinkSlot(declaration, path, DescribeType<T>());
            ASSERT_TRUE(slot.has_value()) << String(path).c_str() << ": " << (slot.has_value() ? String{} : slot.error().message).c_str();
            EXPECT_EQ(slot->level, index);
            const char* address = static_cast<const char*>(context.Object(slot->level)) + slot->offset;
            EXPECT_EQ(static_cast<const void*>(address), static_cast<const void*>(&member)) << String(path).c_str();
        }

        LinkError ExpectRefused(const ContextDecl& declaration, StringView path, const TypeDesc& expected, LinkErrorCode code) {
            const auto slot = LinkSlot(declaration, path, expected);
            EXPECT_FALSE(slot.has_value()) << String(path).c_str();
            if (slot.has_value()) {
                return {};
            }
            EXPECT_EQ(slot.error().code, code) << String(path).c_str() << ": " << slot.error().message.c_str();
            return slot.error();
        }

        /// A context with the list types of the loop programs: `hand` and `refused`
        struct ListLevels {
            ContextDecl declaration;

            ListLevels() {
                EXPECT_TRUE(declaration.Add("hand", "ProgramListHand").has_value());
                EXPECT_TRUE(declaration.Add("refused", "ProgramListRefused").has_value());
            }
        };

        template <typename T, typename Member>
        uint32_t OffsetIn(const T& object, const Member& member) {
            return static_cast<uint32_t>(reinterpret_cast<const char*>(&member) - reinterpret_cast<const char*>(&object));
        }

        LinkError ExpectListRefused(const ContextDecl& declaration, StringView path, LinkErrorCode code) {
            const auto link = LinkList(declaration, path);
            EXPECT_FALSE(link.has_value()) << String(path).c_str();
            if (link.has_value()) {
                return {};
            }
            EXPECT_EQ(link.error().code, code) << String(path).c_str() << ": " << link.error().message.c_str();
            return link.error();
        }

    }  // namespace

    TEST(ValueContextTest, ALevelIsFoundByName) {
        const TwoLevels levels;
        EXPECT_EQ(levels.declaration.Size(), 2U);
        EXPECT_EQ(levels.declaration.IndexOf("caster"), LevelIndex{0});
        EXPECT_EQ(levels.declaration.IndexOf("cast"), LevelIndex{1});
        EXPECT_FALSE(levels.declaration.IndexOf("target").has_value());
        EXPECT_FALSE(levels.declaration.IndexOf("").has_value());
        EXPECT_EQ(levels.declaration.Levels()[0].type->Name(), "ValueUnitStats");
    }

    TEST(ValueContextTest, ALevelIsAddedByTheNameOfItsType) {
        ContextDecl declaration;
        const auto level = declaration.Add("hero", "ValueHeroStats");
        ASSERT_TRUE(level.has_value());
        EXPECT_EQ(*level, LevelIndex{0});
        EXPECT_EQ(declaration.Levels()[0].type->Name(), "ValueHeroStats");
    }

    TEST(ValueContextTest, AnUnknownTypeNameIsRefusedAndNamed) {
        ContextDecl declaration;
        const auto level = declaration.Add("caster", "ValueNoSuchType");
        ASSERT_FALSE(level.has_value());
        EXPECT_EQ(level.error().code, LinkErrorCode::unknown_type);
        EXPECT_TRUE(Contains(level.error().message, "ValueNoSuchType")) << level.error().message.c_str();
        EXPECT_TRUE(Contains(level.error().message, "caster")) << level.error().message.c_str();
        EXPECT_EQ(declaration.Size(), 0U);
    }

    TEST(ValueContextTest, ADuplicateNameAndAnEmptyNameAreRefused) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        const auto duplicate = declaration.Add<ValueCastInfo>("caster");
        ASSERT_FALSE(duplicate.has_value());
        EXPECT_EQ(duplicate.error().code, LinkErrorCode::duplicate_level);
        EXPECT_TRUE(Contains(duplicate.error().message, "caster")) << duplicate.error().message.c_str();
        EXPECT_FALSE(declaration.Add<ValueCastInfo>("").has_value());
        EXPECT_EQ(declaration.Size(), 1U);
    }

    TEST(ValueContextTest, ANinthLevelIsRefused) {
        ContextDecl declaration;
        for (size_t i = 0; i < kMaxContextLevels; ++i) {
            ASSERT_TRUE(declaration.Add<ValueCastInfo>(Format("level{}", i)).has_value()) << i;
        }
        const auto ninth = declaration.Add<ValueCastInfo>("level8");
        ASSERT_FALSE(ninth.has_value());
        EXPECT_EQ(ninth.error().code, LinkErrorCode::too_many_levels);
        EXPECT_TRUE(Contains(ninth.error().message, "level8")) << ninth.error().message.c_str();
        EXPECT_EQ(declaration.Size(), kMaxContextLevels);
    }

    TEST(ValueContextTest, APathIntoAFieldFindsTheMember) {
        const TwoLevels levels;
        const ValueUnitStats stats;
        ExpectSlotFindsMember<bool>(levels.declaration, "caster.alive", 0, stats, stats.alive);
        ExpectSlotFindsMember<int32_t>(levels.declaration, "caster.attack", 0, stats, stats.attack);
        ExpectSlotFindsMember<uint32_t>(levels.declaration, "caster.count", 0, stats, stats.count);
        ExpectSlotFindsMember<int64_t>(levels.declaration, "caster.score", 0, stats, stats.score);
        ExpectSlotFindsMember<float>(levels.declaration, "caster.speed", 0, stats, stats.speed);
        ExpectSlotFindsMember<double>(levels.declaration, "caster.precise", 0, stats, stats.precise);
        ExpectSlotFindsMember<PoolString>(levels.declaration, "caster.title", 0, stats, stats.title);
        ExpectSlotFindsMember<ValueElement>(levels.declaration, "caster.element", 0, stats, stats.element);
        ExpectSlotFindsMember<Vec2>(levels.declaration, "caster.position", 0, stats, stats.position);
        ExpectSlotFindsMember<IVec2>(levels.declaration, "caster.cell", 0, stats, stats.cell);
        ExpectSlotFindsMember<Vec3>(levels.declaration, "caster.velocity", 0, stats, stats.velocity);
        ExpectSlotFindsMember<Vec4>(levels.declaration, "caster.plane", 0, stats, stats.plane);
        ExpectSlotFindsMember<Quat>(levels.declaration, "caster.facing", 0, stats, stats.facing);
        ExpectSlotFindsMember<Rect>(levels.declaration, "caster.bounds", 0, stats, stats.bounds);
        ExpectSlotFindsMember<Color>(levels.declaration, "caster.tint", 0, stats, stats.tint);
        ExpectSlotFindsMember<ValuePoint>(levels.declaration, "caster.spawn", 0, stats, stats.spawn);
    }

    TEST(ValueContextTest, APathIntoAStructHeldByValueFindsTheMember) {
        const TwoLevels levels;
        const ValueUnitStats stats;
        ExpectSlotFindsMember<int32_t>(levels.declaration, "caster.span.low", 0, stats, stats.span.low);
        ExpectSlotFindsMember<int32_t>(levels.declaration, "caster.span.high", 0, stats, stats.span.high);
        ExpectSlotFindsMember<Vec2>(levels.declaration, "caster.span.offset", 0, stats, stats.span.offset);
        ExpectSlotFindsMember<ValueSpan>(levels.declaration, "caster.span", 0, stats, stats.span);
    }

    TEST(ValueContextTest, EveryLevelHasItsOwnObject) {
        const TwoLevels levels;
        const ValueCastInfo cast;
        ExpectSlotFindsMember<int32_t>(levels.declaration, "cast.level", 1, cast, cast.level);
        ExpectSlotFindsMember<Vec3>(levels.declaration, "cast.target", 1, cast, cast.target);

        const ValueUnitStats stats;
        Context context(levels.declaration);
        context.Set(0, stats);
        context.Set(1, cast);
        EXPECT_EQ(context.Object(0), &stats);
        EXPECT_EQ(context.Object(1), &cast);
    }

    TEST(ValueContextTest, AParentsFieldIsReadAsTheLevelsOwn) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueHeroStats>("hero").has_value());
        const ValueHeroStats hero;
        ExpectSlotFindsMember<int32_t>(declaration, "hero.attack", 0, hero, hero.attack);
        ExpectSlotFindsMember<Vec3>(declaration, "hero.velocity", 0, hero, hero.velocity);
        ExpectSlotFindsMember<int32_t>(declaration, "hero.renown", 0, hero, hero.renown);
        ExpectSlotFindsMember<int32_t>(declaration, "hero.span.low", 0, hero, hero.span.low);
    }

    TEST(ValueContextTest, AnObjectOfADerivedTypeIsReadThroughItsBaseLevel) {
        const TwoLevels levels;
        const ValueHeroStats hero;
        // The level declares the base: the field of the base is found in the derived object
        ExpectSlotFindsMember<int32_t>(levels.declaration, "caster.attack", 0, hero, hero.attack);
        ExpectSlotFindsMember<Vec2>(levels.declaration, "caster.span.offset", 0, hero, hero.span.offset);
    }

    TEST(ValueContextTest, ALaterWriteToTheObjectIsSeenThroughTheSlot) {
        const TwoLevels levels;
        ValueUnitStats stats;
        Context context(levels.declaration);
        context.Set(0, stats);
        const auto slot = LinkSlot(levels.declaration, "caster.attack", DescribeType<int32_t>());
        ASSERT_TRUE(slot.has_value());
        const auto read = [&] { return *reinterpret_cast<const int32_t*>(static_cast<const char*>(context.Object(slot->level)) + slot->offset); };
        EXPECT_EQ(read(), 12);
        stats.attack = 99;
        EXPECT_EQ(read(), 99);
    }

    TEST(ValueContextTest, AContextWithoutADeclarationKeepsTheAddress) {
        Context context;
        const ValueCastInfo cast;
        context.Set(3, cast);
        EXPECT_EQ(context.Object(3), &cast);
        EXPECT_EQ(context.Object(2), nullptr);
    }

    TEST(ValueContextTest, AnUnknownLevelNamesTheDeclaredOnesAndTheNearest) {
        const TwoLevels levels;
        const LinkError error = ExpectRefused(levels.declaration, "casster.attack", DescribeType<int32_t>(), LinkErrorCode::unknown_level);
        EXPECT_TRUE(Contains(error.message, "casster.attack")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "unknown level 'casster'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "did you mean 'caster'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "caster, cast")) << error.message.c_str();

        const LinkError far = ExpectRefused(levels.declaration, "view.attack", DescribeType<int32_t>(), LinkErrorCode::unknown_level);
        EXPECT_FALSE(Contains(far.message, "did you mean")) << far.message.c_str();
    }

    TEST(ValueContextTest, ALevelAskedWithoutADeclarationHasNoLevels) {
        const ContextDecl none;
        const LinkError error = ExpectRefused(none, "caster.attack", DescribeType<int32_t>(), LinkErrorCode::unknown_level);
        EXPECT_TRUE(Contains(error.message, "none")) << error.message.c_str();
    }

    TEST(ValueContextTest, AnUnknownFieldNamesTheTypeItsFieldsAndTheNearest) {
        const TwoLevels levels;
        const LinkError error = ExpectRefused(levels.declaration, "caster.attak", DescribeType<int32_t>(), LinkErrorCode::unknown_field);
        EXPECT_TRUE(Contains(error.message, "caster.attak")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "'ValueUnitStats'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "no field 'attak'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "did you mean 'attack'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "alive")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "speed")) << error.message.c_str();
    }

    TEST(ValueContextTest, AnUnknownFieldOfANestedStructNamesThatStruct) {
        const TwoLevels levels;
        const LinkError error = ExpectRefused(levels.declaration, "caster.span.hight", DescribeType<int32_t>(), LinkErrorCode::unknown_field);
        EXPECT_TRUE(Contains(error.message, "'ValueSpan'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "caster.span")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "did you mean 'high'")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "low, high, offset")) << error.message.c_str();
    }

    TEST(ValueContextTest, AMemberOfAValueIsRefused) {
        const TwoLevels levels;
        const LinkError error = ExpectRefused(levels.declaration, "caster.position.x", DescribeType<float>(), LinkErrorCode::unknown_field);
        EXPECT_TRUE(Contains(error.message, "caster.position.x")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "no members")) << error.message.c_str();
    }

    TEST(ValueContextTest, APathBehindAListAMapOrAnOptionalIsNotFixed) {
        ContextDecl declaration;
        ASSERT_TRUE(declaration.Add<ValueBehind>("behind").has_value());
        ASSERT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
        const LinkError list = ExpectRefused(declaration, "behind.spans[1].low", DescribeType<int32_t>(), LinkErrorCode::not_fixed);
        EXPECT_TRUE(Contains(list.message, "behind.spans[1].low")) << list.message.c_str();
        EXPECT_TRUE(Contains(list.message, "[1].low")) << list.message.c_str();
        ExpectRefused(declaration, "behind.table[\"a\"]", DescribeType<int32_t>(), LinkErrorCode::not_fixed);
        ExpectRefused(declaration, "behind.maybe", DescribeType<int32_t>(), LinkErrorCode::type_mismatch);
        ExpectRefused(declaration, "caster.buffs[0]", DescribeType<int32_t>(), LinkErrorCode::not_fixed);
        // A struct held by value is fixed
        EXPECT_TRUE(LinkSlot(declaration, "behind.span.high", DescribeType<int32_t>()).has_value());
    }

    TEST(ValueContextTest, AFieldOfAnotherTypeIsRefusedWithBothTypes) {
        const TwoLevels levels;
        const LinkError number = ExpectRefused(levels.declaration, "caster.attack", DescribeType<float>(), LinkErrorCode::type_mismatch);
        EXPECT_TRUE(Contains(number.message, "caster.attack")) << number.message.c_str();
        EXPECT_TRUE(Contains(number.message, "'attack'")) << number.message.c_str();
        EXPECT_TRUE(Contains(number.message, "int32")) << number.message.c_str();
        EXPECT_TRUE(Contains(number.message, "float")) << number.message.c_str();
        ExpectRefused(levels.declaration, "caster.attack", DescribeType<int64_t>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.attack", DescribeType<uint32_t>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.speed", DescribeType<double>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.title", DescribeType<String>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.element", DescribeType<int32_t>(), LinkErrorCode::type_mismatch);
    }

    TEST(ValueContextTest, AnOpaqueFieldIsRefusedAsAnotherOpaqueTypeAndBothAreNamed) {
        const TwoLevels levels;
        const LinkError error = ExpectRefused(levels.declaration, "caster.position", DescribeType<Vec3>(), LinkErrorCode::type_mismatch);
        EXPECT_TRUE(Contains(error.message, "Vec2")) << error.message.c_str();
        EXPECT_TRUE(Contains(error.message, "Vec3")) << error.message.c_str();
        ExpectRefused(levels.declaration, "caster.velocity", DescribeType<Vec4>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.cell", DescribeType<Vec2>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.tint", DescribeType<Rect>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.spawn", DescribeType<IVec2>(), LinkErrorCode::type_mismatch);
        ExpectRefused(levels.declaration, "caster.span", DescribeType<ValueCastInfo>(), LinkErrorCode::type_mismatch);
    }

    TEST(ValueContextTest, ABadPathTextIsRefused) {
        const TwoLevels levels;
        for (const StringView text : {"", "caster.", ".attack", "caster..attack", "caster.attack[", "9caster.attack", "caster attack", "caster.a-b"}) {
            ExpectRefused(levels.declaration, text, DescribeType<int32_t>(), LinkErrorCode::invalid_path);
        }
        const LinkError level = ExpectRefused(levels.declaration, "caster", DescribeType<int32_t>(), LinkErrorCode::invalid_path);
        EXPECT_TRUE(Contains(level.message, "level")) << level.message.c_str();
        ExpectRefused(levels.declaration, "caster[0]", DescribeType<int32_t>(), LinkErrorCode::invalid_path);
    }

    TEST(ValueContextTest, ALinkRefusalChangesNothingInTheDeclaration) {
        const TwoLevels levels;
        ExpectRefused(levels.declaration, "caster.attak", DescribeType<int32_t>(), LinkErrorCode::unknown_field);
        EXPECT_EQ(levels.declaration.Size(), 2U);
        EXPECT_TRUE(LinkSlot(levels.declaration, "caster.attack", DescribeType<int32_t>()).has_value());
    }

    TEST(ValueContextTest, AListFieldOfEveryContainerIsLinkedAtTheOffsetTheDescriptorTakes) {
        const ListLevels levels;
        const ProgramListHand hand{};
        const struct {
            StringView path;
            uint32_t offset;
            size_t stride;
            ListLayout layout;
            uint32_t count;
        } rows[] = {
            {"hand.tiles", OffsetIn(hand, hand.tiles), sizeof(ProgramListTile), ListLayout::Inline, 14},
            {"hand.sets", OffsetIn(hand, hand.sets), sizeof(ProgramListSet), ListLayout::Pointers, 0},
            {"hand.counts", OffsetIn(hand, hand.counts), sizeof(int32_t), ListLayout::Pointers, 0},
            {"hand.weights", OffsetIn(hand, hand.weights), sizeof(float), ListLayout::Pointers, 0},
        };
        for (const auto& row : rows) {
            const auto link = LinkList(levels.declaration, row.path);
            ASSERT_TRUE(link.has_value()) << String(row.path).c_str() << ": " << (link.has_value() ? String{} : link.error().message).c_str();
            EXPECT_EQ(link->level, LevelIndex{0}) << String(row.path).c_str();
            EXPECT_EQ(link->offset, row.offset) << String(row.path).c_str();
            const auto descriptor = MakeListDescriptor(link->offset, *link->list);
            ASSERT_TRUE(descriptor.has_value()) << String(row.path).c_str();
            EXPECT_EQ(descriptor->stride, row.stride) << String(row.path).c_str();
            EXPECT_EQ(descriptor->layout, row.layout) << String(row.path).c_str();
            EXPECT_EQ(descriptor->count, row.count) << String(row.path).c_str();
        }
        const auto sets = LinkList(levels.declaration, "hand.sets");
        ASSERT_TRUE(sets.has_value());
        const auto element = DescribeListElement(*sets->list);
        ASSERT_TRUE(element.has_value());
        ASSERT_NE(*element, nullptr);
        EXPECT_EQ((*element)->Name(), "ProgramListSet");
    }

    TEST(ValueContextTest, AListThatCrossesAnEmbeddedObjectIsLinkedAtTheSumOfTheOffsets) {
        const ListLevels levels;
        const ProgramListRefused refused{};
        const auto link = LinkList(levels.declaration, "refused.set.tiles");
        ASSERT_TRUE(link.has_value()) << (link.has_value() ? String{} : link.error().message).c_str();
        EXPECT_EQ(link->level, LevelIndex{1});
        EXPECT_EQ(link->offset, OffsetIn(refused, refused.set.tiles));
    }

    TEST(ValueContextTest, ElementsOfEveryAllowedKindAreDescribed) {
        const ListLevels levels;
        for (const StringView path : {"hand.counts", "hand.weights", "hand.flags", "refused.names", "refused.points"}) {
            const auto link = LinkList(levels.declaration, path);
            ASSERT_TRUE(link.has_value()) << String(path).c_str() << ": " << (link.has_value() ? String{} : link.error().message).c_str();
            const auto element = DescribeListElement(*link->list);
            ASSERT_TRUE(element.has_value()) << String(path).c_str();
            EXPECT_EQ(*element, nullptr) << String(path).c_str() << ": a scalar has no type info";
        }
        const auto tiles = LinkList(levels.declaration, "hand.tiles");
        ASSERT_TRUE(tiles.has_value());
        const auto tile = DescribeListElement(*tiles->list);
        ASSERT_TRUE(tile.has_value());
        ASSERT_NE(*tile, nullptr);
        EXPECT_EQ((*tile)->Name(), "ProgramListTile");
    }

    TEST(ValueContextTest, AFieldOfAnElementIsLinkedAtItsOffset) {
        const ListLevels levels;
        const auto tiles = LinkList(levels.declaration, "hand.tiles");
        ASSERT_TRUE(tiles.has_value());
        const TypeInfo& tile = **DescribeListElement(*tiles->list);
        const ProgramListTile sample{};
        const struct {
            StringView path;
            uint32_t offset;
            TypeKind kind;
        } rows[] = {
            {"suit", OffsetIn(sample, sample.suit), TypeKind::Enum},      {"terminal", OffsetIn(sample, sample.terminal), TypeKind::Bool},   {"rank", OffsetIn(sample, sample.rank), TypeKind::Int32},
            {"weight", OffsetIn(sample, sample.weight), TypeKind::Float}, {"position", OffsetIn(sample, sample.position), TypeKind::Opaque}, {"name", OffsetIn(sample, sample.name), TypeKind::String},
        };
        for (const auto& row : rows) {
            const auto link = LinkElementField(tile, row.path);
            ASSERT_TRUE(link.has_value()) << String(row.path).c_str() << ": " << (link.has_value() ? String{} : link.error().message).c_str();
            EXPECT_EQ(link->offset, row.offset) << String(row.path).c_str();
            EXPECT_EQ(link->type->kind, row.kind) << String(row.path).c_str();
        }

        const auto sets = LinkList(levels.declaration, "hand.sets");
        ASSERT_TRUE(sets.has_value());
        const TypeInfo& set = **DescribeListElement(*sets->list);
        const ProgramListSet setSample{};
        const auto nested = LinkElementField(set, "tiles");
        ASSERT_TRUE(nested.has_value()) << (nested.has_value() ? String{} : nested.error().message).c_str();
        EXPECT_EQ(nested->offset, OffsetIn(setSample, setSample.tiles));
        ASSERT_EQ(nested->type->kind, TypeKind::List) << "a list field of an element is linked as it is";
        const auto elementDescriptor = MakeListDescriptor(nested->offset, *nested->type);
        ASSERT_TRUE(elementDescriptor.has_value());
        EXPECT_EQ(elementDescriptor->count, 4U);
    }

    TEST(ValueContextTest, AListRefusalNamesThePathAndWhatIsWrong) {
        const ListLevels levels;
        const LinkError scalar = ExpectListRefused(levels.declaration, "hand.before", LinkErrorCode::not_a_list);
        EXPECT_TRUE(Contains(scalar.message, "hand.before")) << scalar.message.c_str();
        EXPECT_TRUE(Contains(scalar.message, "int32")) << scalar.message.c_str();
        EXPECT_TRUE(Contains(scalar.message, "list field")) << scalar.message.c_str();
        ExpectListRefused(levels.declaration, "hand.tiles[1]", LinkErrorCode::not_fixed);
        const LinkError behind = ExpectListRefused(levels.declaration, "hand.sets[1].tiles", LinkErrorCode::not_fixed);
        EXPECT_TRUE(Contains(behind.message, "[1].tiles")) << behind.message.c_str();

        const LinkError level = ExpectListRefused(levels.declaration, "hand", LinkErrorCode::invalid_path);
        EXPECT_TRUE(Contains(level.message, "is a level")) << level.message.c_str();
        const LinkError unknownLevel = ExpectListRefused(levels.declaration, "hnd.tiles", LinkErrorCode::unknown_level);
        EXPECT_TRUE(Contains(unknownLevel.message, "did you mean 'hand'")) << unknownLevel.message.c_str();
        EXPECT_TRUE(Contains(unknownLevel.message, "hand, refused")) << unknownLevel.message.c_str();
        const LinkError unknownField = ExpectListRefused(levels.declaration, "hand.tilez", LinkErrorCode::unknown_field);
        EXPECT_TRUE(Contains(unknownField.message, "did you mean 'tiles'")) << unknownField.message.c_str();
        EXPECT_TRUE(Contains(unknownField.message, "ProgramListHand")) << unknownField.message.c_str();
        EXPECT_TRUE(Contains(unknownField.message, "level 'hand'")) << unknownField.message.c_str();
        ExpectListRefused(levels.declaration, "hand..tiles", LinkErrorCode::invalid_path);
    }

    TEST(ValueContextTest, AListOfWhatAFormulaCannotReadIsRefusedWithWhatIsAllowed) {
        const ListLevels levels;
        for (const StringView path : {"refused.texts"}) {
            const LinkError error = ExpectListRefused(levels.declaration, path, LinkErrorCode::bad_element);
            EXPECT_TRUE(Contains(error.message, path)) << error.message.c_str();
            EXPECT_TRUE(Contains(error.message, "PoolStrings")) << error.message.c_str();
            EXPECT_TRUE(Contains(error.message, "reflected objects")) << error.message.c_str();
        }
    }

    TEST(ValueContextTest, ADescribedListTypeIsRefusedWithoutAPathToo) {
        const auto lists = DescribeListElement(DescribeType<Vector<Vector<int32_t>>>());
        ASSERT_FALSE(lists.has_value());
        EXPECT_EQ(lists.error().code, LinkErrorCode::bad_element);
        EXPECT_TRUE(Contains(lists.error().message, "list of int32")) << lists.error().message.c_str();
        EXPECT_EQ(DescribeListElement(DescribeType<Vector<Optional<int32_t>>>()).error().code, LinkErrorCode::bad_element);
        EXPECT_EQ(DescribeListElement(DescribeType<Vector<HashMap<String, int32_t>>>()).error().code, LinkErrorCode::bad_element);
        EXPECT_TRUE(DescribeListElement(DescribeType<Vector<ProgramListSuit>>()).has_value());
        EXPECT_TRUE(DescribeListElement(DescribeType<Vector<PoolString>>()).has_value());
        EXPECT_TRUE(DescribeListElement(DescribeType<Vector<Vec3>>()).has_value());

        const auto notList = DescribeListElement(DescribeType<int32_t>());
        ASSERT_FALSE(notList.has_value());
        EXPECT_EQ(notList.error().code, LinkErrorCode::not_a_list);
    }

    TEST(ValueContextTest, AnElementFieldRefusalNamesTheTypeAndItsFields) {
        const ListLevels levels;
        const TypeInfo& tile = **DescribeListElement(*LinkList(levels.declaration, "hand.tiles")->list);
        const auto misspelt = LinkElementField(tile, "ranc");
        ASSERT_FALSE(misspelt.has_value());
        EXPECT_EQ(misspelt.error().code, LinkErrorCode::unknown_field);
        EXPECT_TRUE(Contains(misspelt.error().message, "did you mean 'rank'")) << misspelt.error().message.c_str();
        EXPECT_TRUE(Contains(misspelt.error().message, "'ProgramListTile'")) << misspelt.error().message.c_str();
        EXPECT_TRUE(Contains(misspelt.error().message, "terminal")) << misspelt.error().message.c_str();

        const auto member = LinkElementField(tile, "position.x");
        ASSERT_FALSE(member.has_value());
        EXPECT_EQ(member.error().code, LinkErrorCode::unknown_field);

        const auto indexed = LinkElementField(tile, "[0]");
        ASSERT_FALSE(indexed.has_value());
        EXPECT_EQ(indexed.error().code, LinkErrorCode::invalid_path);
        const auto empty = LinkElementField(tile, "");
        ASSERT_FALSE(empty.has_value());
        EXPECT_EQ(empty.error().code, LinkErrorCode::invalid_path);

        const TypeInfo& hand = *levels.declaration.Levels()[0].type;
        const auto behind = LinkElementField(hand, "sets[0].pung");
        ASSERT_FALSE(behind.has_value());
        EXPECT_EQ(behind.error().code, LinkErrorCode::not_fixed);
    }

}  // namespace BECore::Tests
