// Derived (Values/Derived/): a value of any type that rebuilds only when a declared input moved, and the DerivedReads that declare the inputs. The context types
// are those of ValueTestTypes.h, each level with a counter its owner bumps.

#include <BECore/Assert/AssertEvent.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Derived/DerivedReads.h>
#include <Common/AssertBridge.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace ValueTestData;

    namespace {

        constexpr LevelIndex kCasterLevel = 0;
        constexpr LevelIndex kCastLevel = 1;
        constexpr LevelMask kCasterBit = 0b01;
        constexpr LevelMask kCastBit = 0b10;

        /// A result type that is neither a string nor a list: a layout made of two numbers
        struct Layout {
            int32_t width = 0;
            int32_t height = 0;

            [[nodiscard]] bool operator==(const Layout&) const = default;
        };

        /// Both levels of a `ValueWorld` with a counter each, a value that reads each, and a context a game rebuilds each frame
        struct DerivedWorld {
            ContextDecl declaration;
            ValueUnitStats caster;
            ValueCastInfo cast;
            uint64_t casterVersion = 1;
            uint64_t castVersion = 1;
            Value<int32_t> attack;
            Value<int32_t> level;

            DerivedWorld() {
                EXPECT_TRUE(declaration.Add<ValueUnitStats>("caster").has_value());
                EXPECT_TRUE(declaration.Add<ValueCastInfo>("cast").has_value());
                EXPECT_TRUE(attack.Link(declaration, "caster.attack").has_value());
                EXPECT_TRUE(level.Link(declaration, "cast.level").has_value());
            }

            DerivedWorld(const DerivedWorld&) = delete;
            DerivedWorld& operator=(const DerivedWorld&) = delete;

            [[nodiscard]] Context Make() const {
                Context context(declaration);
                context.Set(kCasterLevel, caster, &casterVersion);
                context.Set(kCastLevel, cast, &castVersion);
                return context;
            }

            [[nodiscard]] DerivedReads ReadsCaster() const {
                DerivedReads reads;
                reads.Add(attack);
                return reads;
            }
        };

        void BuildAttackText(const DerivedWorld& world, String& out, const Context& view) {
            out.clear();
            out += Format("attack {}", world.attack.Get(view));
        }

    }  // namespace

    // ---- DerivedReads ----

    TEST(ValueDerivedReadsTest, TheMaskIsTheUnionOfTheValuesTheLevelsAndTheMasksAdded) {
        DerivedWorld world;
        DerivedReads reads;
        EXPECT_TRUE(reads.IsEmpty());
        reads.Add(Value<int32_t>(5));
        EXPECT_EQ(reads.Mask(), 0) << "a Const reads no level";
        EXPECT_TRUE(reads.IsEmpty());
        reads.Add(world.level);
        EXPECT_EQ(reads.Mask(), kCastBit);
        reads.AddLevel(kCasterLevel);
        EXPECT_EQ(reads.Mask(), kCasterBit | kCastBit);
        EXPECT_FALSE(reads.IsEmpty());

        DerivedReads fromMask;
        fromMask.AddLevels(ValueDependencies().Add(world.attack).Mask());
        EXPECT_EQ(fromMask.Mask(), kCasterBit);
    }

    TEST(ValueDerivedReadsTest, ALevelByNameIsCheckedAgainstTheDeclaration) {
        DerivedWorld world;
        DerivedReads reads;
        const auto known = reads.AddLevel(world.declaration, "cast");
        ASSERT_TRUE(known.has_value());
        EXPECT_EQ(reads.Mask(), kCastBit);

        const auto unknown = reads.AddLevel(world.declaration, "casster");
        ASSERT_FALSE(unknown.has_value());
        EXPECT_EQ(unknown.error().code, LinkErrorCode::unknown_level);
        EXPECT_NE(unknown.error().message.find("'casster'"), String::npos) << unknown.error().message.c_str();
        EXPECT_NE(unknown.error().message.find("did you mean"), String::npos) << unknown.error().message.c_str();
        EXPECT_NE(unknown.error().message.find("caster, cast"), String::npos) << "the declared levels are listed: " << unknown.error().message.c_str();
        EXPECT_EQ(reads.Mask(), kCastBit) << "a refused level adds nothing";
    }

    TEST(ValueDerivedReadsTest, AFifthSourceIsRefusedNotDropped) {
        Array<uint64_t, kMaxVersionSources + 1> counters{};
        DerivedReads reads;
        for (size_t i = 0; i < kMaxVersionSources; ++i) {
            EXPECT_TRUE(reads.AddSource(counters[i]));
        }
        size_t asserts = 0;
        {
            AssertBridge::Capture capture([&](const AssertEvent&) { ++asserts; });
            EXPECT_FALSE(reads.AddSource(counters[kMaxVersionSources]));
        }
#if ENGINE_ENABLE_ASSERTS
        EXPECT_EQ(asserts, 1U);
#endif
        ASSERT_EQ(reads.Sources().size(), kMaxVersionSources);
        EXPECT_EQ(reads.Sources()[0], &counters[0]);
        EXPECT_EQ(reads.Sources()[kMaxVersionSources - 1], &counters[kMaxVersionSources - 1]);
    }

    TEST(ValueDerivedReadsTest, RestrictedKeepsTheAskedLevelsAndClearsTheOthers) {
        DerivedWorld world;
        const Context context = world.Make();
        const Context view = context.Restricted(kCasterBit);

        EXPECT_EQ(view.Object(kCasterLevel), context.Object(kCasterLevel));
        EXPECT_EQ(view.Version(kCasterLevel), context.Version(kCasterLevel));
        EXPECT_EQ(view.VersionKey(kCasterBit), context.VersionKey(kCasterBit));
        EXPECT_EQ(view.Object(kCastLevel), nullptr);
        EXPECT_EQ(view.Version(kCastLevel), 0U);
        EXPECT_FALSE(view.IsVersioned(kCastBit));
        EXPECT_TRUE(view.IsVersioned(kCasterBit));
        EXPECT_NE(context.Object(kCastLevel), nullptr) << "the original is not touched";
    }

    TEST(ValueDerivedReadsTest, RestrictedKeepsTheWritableMarkOfAKeptLevelOnly) {
        DerivedWorld world;
        Context context(world.declaration);
        context.SetMutable(kCasterLevel, world.caster, &world.casterVersion);
        context.SetMutable(kCastLevel, world.cast, &world.castVersion);
        const Context view = context.Restricted(kCastBit);
        EXPECT_FALSE(view.IsMutable(kCasterLevel));
        EXPECT_TRUE(view.IsMutable(kCastLevel));
        EXPECT_EQ(view.VersionKey(kCastBit), context.VersionKey(kCastBit));
    }

}  // namespace BECore::Tests
