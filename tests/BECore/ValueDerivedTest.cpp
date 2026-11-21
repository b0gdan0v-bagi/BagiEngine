// Derived (Values/Derived/): a value of any type that rebuilds only when a declared input moved, and the DerivedReads that declare the inputs. The context types
// are those of ValueTestTypes.h, each level with a counter its owner bumps.

#include <BECore/Assert/AssertEvent.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/ValueTestTypes.h>
#include <BECore/Values/Derived/Derived.h>
#include <BECore/Values/Derived/DerivedReads.h>
#include <Common/AllocCheck.h>
#include <Common/AssertBridge.h>
#include <Common/LogRecorder.h>
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

    // ---- Derived ----

    TEST(ValueDerivedTest, TheFirstCallBuildsOnceAndTheNextOnesHit) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };

        EXPECT_EQ(text.Get(context, build), "attack 12");
        EXPECT_EQ(text.Rebuilds(), 1U);
        EXPECT_EQ(text.Get(context, build), "attack 12");
        EXPECT_EQ(text.Get(context, build), "attack 12");
        EXPECT_EQ(text.Rebuilds(), 1U);
    }

    TEST(ValueDerivedTest, ABumpOfADeclaredLevelRebuildsOnceAndTheNextCallHits) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        static_cast<void>(text.Get(context, build));

        world.caster.attack = 30;
        EXPECT_EQ(text.Get(context, build), "attack 12") << "no bump, no rebuild: the owner says when the object changed";
        ++world.casterVersion;
        EXPECT_EQ(text.Get(context, build), "attack 30");
        EXPECT_EQ(text.Rebuilds(), 2U);
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 2U);
    }

    TEST(ValueDerivedTest, ABumpOfAnUndeclaredLevelNeverRebuilds) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        static_cast<void>(text.Get(context, build));
        for (int32_t i = 0; i < 5; ++i) {
            ++world.castVersion;
            static_cast<void>(text.Get(context, build));
        }
        EXPECT_EQ(text.Rebuilds(), 1U);
    }

    TEST(ValueDerivedTest, AContextRebuiltEachFrameOverTheSameObjectsAndCountersHits) {
        DerivedWorld world;
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        for (int32_t frame = 0; frame < 6; ++frame) {
            const Context context = world.Make();
            static_cast<void>(text.Get(context, build));
        }
        EXPECT_EQ(text.Rebuilds(), 1U);
    }

    TEST(ValueDerivedTest, AnotherObjectOrAnotherCounterAtADeclaredLevelRebuilds) {
        DerivedWorld world;
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        static_cast<void>(text.Get(world.Make(), build));

        ValueUnitStats other;
        other.attack = 99;
        Context swapped(world.declaration);
        swapped.Set(kCasterLevel, other, &world.casterVersion);
        swapped.Set(kCastLevel, world.cast, &world.castVersion);
        EXPECT_EQ(text.Get(swapped, build), "attack 99");
        EXPECT_EQ(text.Rebuilds(), 2U);

        uint64_t otherCounter = world.casterVersion;
        Context recounted(world.declaration);
        recounted.Set(kCasterLevel, other, &otherCounter);
        recounted.Set(kCastLevel, world.cast, &world.castVersion);
        static_cast<void>(text.Get(recounted, build));
        EXPECT_EQ(text.Rebuilds(), 3U) << "the same number in another counter is another input";
    }

    TEST(ValueDerivedTest, TwoLevelsMoveIndependently) {
        DerivedWorld world;
        DerivedReads reads;
        reads.Add(world.attack).Add(world.level);
        Derived<String> text(std::move(reads));
        const auto build = [&](String& out, const Context& view) {
            out.clear();
            out += Format("{}/{}", world.attack.Get(view), world.level.Get(view));
        };
        const Context context = world.Make();
        EXPECT_EQ(text.Get(context, build), "12/3");
        ++world.casterVersion;
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 2U);
        ++world.castVersion;
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 3U);
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 3U);
    }

    TEST(ValueDerivedTest, ASourceCounterThatMovesRebuildsAndTwoSourcesAreIndependent) {
        uint64_t first = 5;
        uint64_t second = 7;
        DerivedReads reads;
        ASSERT_TRUE(reads.AddSource(first));
        ASSERT_TRUE(reads.AddSource(second));
        Derived<Vector<int32_t>> list(std::move(reads));
        const Context none;
        const auto build = [&](Vector<int32_t>& out, const Context&) {
            out.clear();
            out.push_back(static_cast<int32_t>(first));
            out.push_back(static_cast<int32_t>(second));
        };

        EXPECT_EQ(list.Get(none, build).size(), 2U);
        static_cast<void>(list.Get(none, build));
        EXPECT_EQ(list.Rebuilds(), 1U);
        ++first;
        EXPECT_EQ(list.Get(none, build)[0], 6);
        EXPECT_EQ(list.Rebuilds(), 2U);
        ++second;
        EXPECT_EQ(list.Get(none, build)[1], 8);
        EXPECT_EQ(list.Rebuilds(), 3U);
        static_cast<void>(list.Get(none, build));
        EXPECT_EQ(list.Rebuilds(), 3U);
    }

    TEST(ValueDerivedTest, ASourceAndALevelTogetherRebuildOnEither) {
        DerivedWorld world;
        uint64_t source = 1;
        DerivedReads reads = world.ReadsCaster();
        ASSERT_TRUE(reads.AddSource(source));
        Derived<Layout> layout(std::move(reads));
        const Context context = world.Make();
        const auto build = [&](Layout& out, const Context& view) {
            out.width = world.attack.Get(view);
            out.height = static_cast<int32_t>(source);
        };
        EXPECT_EQ(layout.Get(context, build), (Layout{12, 1}));
        ++source;
        EXPECT_EQ(layout.Get(context, build), (Layout{12, 2}));
        ++world.casterVersion;
        static_cast<void>(layout.Get(context, build));
        EXPECT_EQ(layout.Rebuilds(), 3U);
    }

    TEST(ValueDerivedTest, ABuilderThatBumpsItsOwnCounterDoesNotRebuildForever) {
        uint64_t counter = 1;
        DerivedReads reads;
        ASSERT_TRUE(reads.AddSource(counter));
        Derived<int32_t> value(std::move(reads));
        const Context none;
        const auto build = [&](int32_t& out, const Context&) {
            ++counter;
            ++out;
        };
        static_cast<void>(value.Get(none, build));
        static_cast<void>(value.Get(none, build));
        static_cast<void>(value.Get(none, build));
        EXPECT_EQ(value.Rebuilds(), 1U) << "the counter is read after the build, so the builder's own bump is part of the key";
        ++counter;
        static_cast<void>(value.Get(none, build));
        EXPECT_EQ(value.Rebuilds(), 2U) << "another owner's bump is seen";
    }

    TEST(ValueDerivedTest, AnEmptyDeclarationBuildsOnceUntilInvalidatedOrReset) {
        Derived<int32_t> value{DerivedReads{}};
        const Context none;
        int32_t calls = 0;
        const auto build = [&](int32_t& out, const Context&) { out = ++calls; };
        EXPECT_EQ(value.Get(none, build), 1);
        EXPECT_EQ(value.Get(none, build), 1);
        value.Invalidate();
        EXPECT_EQ(value.Get(none, build), 2);
        value.Reset(DerivedReads{});
        EXPECT_EQ(value.Get(none, build), 3);
        EXPECT_EQ(value.Get(none, build), 3);
    }

    TEST(ValueDerivedTest, InvalidateAndResetOfADeclaredValue) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        static_cast<void>(text.Get(context, build));
        text.Invalidate();
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 2U);

        DerivedReads both = world.ReadsCaster();
        both.Add(world.level);
        text.Reset(std::move(both));
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 3U);
        ++world.castVersion;
        static_cast<void>(text.Get(context, build));
        EXPECT_EQ(text.Rebuilds(), 4U) << "the new declaration is in force";
    }

    TEST(ValueDerivedTest, ADeclaredLevelWithNoCounterRebuildsOnEveryCallAndSaysSoOnce) {
        DerivedWorld world;
        Context unversioned(world.declaration);
        unversioned.Set(kCasterLevel, world.caster);
        unversioned.Set(kCastLevel, world.cast, &world.castVersion);
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };

        LogRecorder recorder;
        constexpr uint32_t kCalls = 5;
        for (uint32_t i = 0; i < kCalls; ++i) {
            world.caster.attack = static_cast<int32_t>(i);
            EXPECT_EQ(text.Get(unversioned, build), Format("attack {}", i));
            EXPECT_EQ(text.Rebuilds(), i + 1);
        }
        LogEvent::Flush();
#if ENGINE_ENABLE_ASSERTS
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Derived"}), 1U);
#else
        EXPECT_EQ(recorder.Count(LogLevel::Warning, {"Derived"}), 0U);
#endif

        const Context versioned = world.Make();
        static_cast<void>(text.Get(versioned, build));
        static_cast<void>(text.Get(versioned, build));
        EXPECT_EQ(text.Rebuilds(), kCalls + 1) << "a context that can be keyed starts caching, whatever was built before";
    }

    TEST(ValueDerivedTest, ABuilderSeesOnlyTheDeclaredLevelsWhereAssertsAreOn) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<int32_t> value(world.ReadsCaster());
        bool sawCaster = false;
        bool sawCast = false;
        static_cast<void>(value.Get(context, [&](int32_t&, const Context& view) {
            sawCaster = view.Object(kCasterLevel) != nullptr;
            sawCast = view.Object(kCastLevel) != nullptr;
        }));
        EXPECT_TRUE(sawCaster);
#if ENGINE_ENABLE_ASSERTS
        EXPECT_FALSE(sawCast) << "a Value::Get of an undeclared level meets a null level and fails the ASSERT";
#else
        EXPECT_TRUE(sawCast);
#endif
    }

    TEST(ValueDerivedTest, TheCountOfRebuildsOverRandomFramesIsTheNumberOfFramesWhereADeclaredInputMoved) {
        DerivedWorld world;
        uint64_t source = 1;
        DerivedReads reads = world.ReadsCaster();
        ASSERT_TRUE(reads.AddSource(source));
        Derived<String> text(std::move(reads));
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };

        uint32_t seed = 12345;
        const auto next = [&seed](uint32_t bound) {
            seed = seed * 1664525U + 1013904223U;
            return (seed >> 16) % bound;
        };
        constexpr uint32_t kFrames = 1000;
        uint32_t expected = 0;
        for (uint32_t frame = 0; frame < kFrames; ++frame) {
            bool moved = false;
            const uint32_t pick = next(6);
            if (pick == 0) {
                ++world.casterVersion;
                moved = true;
            } else if (pick == 1) {
                ++source;
                moved = true;
            } else if (pick == 2) {
                ++world.castVersion;
            }
            if (pick == 3) {
                ++world.casterVersion;
                ++source;
                moved = true;
            }
            expected += (moved || frame == 0) ? 1 : 0;
            static_cast<void>(text.Get(world.Make(), build));
            ASSERT_EQ(text.Rebuilds(), expected) << "frame " << frame;
        }
    }

    TEST(ValueDerivedTest, AHitAllocatesNothingAndARebuildThatFitsItsCapacityAllocatesNothing) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> text(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) {
            out.clear();
            out += "attack ";
            out += static_cast<char>('0' + world.attack.Get(view) % 10);
        };
        static_cast<void>(text.Get(context, build));

        ExpectNoAllocations([&] {
            for (int32_t i = 0; i < 100; ++i) {
                static_cast<void>(text.Get(context, build));
            }
        });
        ExpectNoAllocations([&] {
            ++world.casterVersion;
            static_cast<void>(text.Get(context, build));
        });
        EXPECT_EQ(text.Rebuilds(), 2U);
    }

    TEST(ValueDerivedTest, AMoveKeepsTheResultAndTheKey) {
        DerivedWorld world;
        const Context context = world.Make();
        Derived<String> original(world.ReadsCaster());
        const auto build = [&](String& out, const Context& view) { BuildAttackText(world, out, view); };
        static_cast<void>(original.Get(context, build));

        Derived<String> moved(std::move(original));
        EXPECT_EQ(moved.Get(context, build), "attack 12");
        EXPECT_EQ(moved.Rebuilds(), 1U);
        ++world.casterVersion;
        static_cast<void>(moved.Get(context, build));
        EXPECT_EQ(moved.Rebuilds(), 2U);
    }

}  // namespace BECore::Tests
