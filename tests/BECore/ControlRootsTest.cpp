#include <BECore/Control/ControlRoots.h>
#include <BECore/Control/ControlService.h>
#include <BECore/PoolString/PoolString.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A root whose provider hands out @p object under @p type
        ControlRoot RootOf(int& object, const TypeInfo& type, bool allowInterning = false) {
            return ControlRoot{[&object, &type] { return TypedRef{&object, &type}; }, allowInterning};
        }

    }  // namespace

    TEST(ControlRootsTest, StartsEmpty) {
        const ControlRoots roots;
        EXPECT_TRUE(roots.Names().empty());
        EXPECT_EQ(roots.Find("anything"), nullptr);
        EXPECT_FALSE(roots.Resolve("anything"));
    }

    TEST(ControlRootsTest, ARegisteredRootResolvesToItsObject) {
        int object = 0;
        const TypeInfo type;
        ControlRoots roots;
        roots.Register("player", RootOf(object, type));

        const ControlRoot* found = roots.Find("player");
        ASSERT_NE(found, nullptr);
        EXPECT_FALSE(found->allowInterning);
        const TypedRef ref = roots.Resolve("player");
        ASSERT_TRUE(ref);
        EXPECT_EQ(ref.Address(), &object);
        EXPECT_EQ(ref.type, &type);
    }

    TEST(ControlRootsTest, RegisteringAgainReplacesTheRoot) {
        int first = 0;
        int second = 0;
        const TypeInfo type;
        ControlRoots roots;
        roots.Register("player", RootOf(first, type));
        roots.Register("player", RootOf(second, type, true));

        EXPECT_EQ(roots.Names().size(), 1U);
        EXPECT_EQ(roots.Resolve("player").Address(), &second);
        ASSERT_NE(roots.Find("player"), nullptr);
        EXPECT_TRUE(Require(roots.Find("player")).allowInterning);
    }

    TEST(ControlRootsTest, TheProviderRunsOnEveryResolve) {
        int object = 0;
        int calls = 0;
        const TypeInfo type;
        ControlRoots roots;
        roots.Register("player", ControlRoot{[&] {
                           ++calls;
                           return calls == 2 ? TypedRef{} : TypedRef{&object, &type};
                       }});

        EXPECT_TRUE(roots.Resolve("player"));
        EXPECT_FALSE(roots.Resolve("player"));
        EXPECT_TRUE(roots.Resolve("player"));
        EXPECT_EQ(calls, 3);
    }

    TEST(ControlRootsTest, UnknownAndEmptyRootsGiveNothing) {
        ControlRoots roots;
        roots.Register("empty", ControlRoot{});

        EXPECT_NE(roots.Find("empty"), nullptr);
        EXPECT_FALSE(roots.Resolve("empty"));
        EXPECT_EQ(roots.Find("missing"), nullptr);
        EXPECT_FALSE(roots.Resolve("missing"));
    }

    TEST(ControlRootsTest, NamesAreSortedWhateverTheOrderOfRegistration) {
        ControlRoots roots;
        roots.Register("scene", ControlRoot{});
        roots.Register("game", ControlRoot{});
        roots.Register("audio", ControlRoot{});

        const Vector<String> names = roots.Names();
        ASSERT_EQ(names.size(), 3U);
        EXPECT_EQ(names[0], "audio");
        EXPECT_EQ(names[1], "game");
        EXPECT_EQ(names[2], "scene");
    }

    TEST(ControlRootsTest, LookingUpAnUnknownNameDoesNotIntern) {
        const ControlRoots roots;
        EXPECT_EQ(roots.Find("client_only_root_a41c07"), nullptr);
        EXPECT_FALSE(roots.Resolve("client_only_root_a41c07"));
        EXPECT_FALSE(PoolString::Find(StringView{"client_only_root_a41c07"}).has_value());
    }

    TEST(ControlRootsTest, ARootNameIsNotInterned) {
        ControlRoots roots;
        roots.Register("registered_root_5be913", ControlRoot{});
        EXPECT_FALSE(PoolString::Find(StringView{"registered_root_5be913"}).has_value());
    }

    TEST(ControlRootsTest, AServiceOwnsItsRoots) {
        ControlService first;
        ControlService second;
        first.Roots().Register("game", ControlRoot{});

        EXPECT_NE(first.Roots().Find("game"), nullptr);
        EXPECT_EQ(second.Roots().Find("game"), nullptr);
    }

}  // namespace BECore::Tests
