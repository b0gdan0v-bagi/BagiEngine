#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {
        class Keeper;
        class Stranger {};

        struct Door {
            void Open(PassKey<Keeper>) {
                ++opened;
            }

            int opened = 0;
        };

        class Keeper {
        public:
            static void OpenWithBraces(Door& door) {
                door.Open({});
            }

            static void OpenWithNamedKey(Door& door) {
                PassKey<Keeper> key;
                door.Open(std::move(key));
            }
        };

        class Apprentice : public Keeper {
        public:
            static void OpenWithOwnKey(Door& door) {
                door.Open(PassKey<Apprentice>{});
            }
        };
    }  // namespace

    // Compile-time checks: a regression breaks the build.

    // A class derived from the key could construct it and pass it where the key is asked
    static_assert(std::is_final_v<PassKey<Keeper>>);
    static_assert(!std::is_default_constructible_v<PassKey<Keeper>>);

    static_assert(std::is_move_constructible_v<PassKey<Keeper>>);
    static_assert(!std::is_copy_constructible_v<PassKey<Keeper>>);
    static_assert(!std::is_copy_assignable_v<PassKey<Keeper>>);
    static_assert(!std::is_move_assignable_v<PassKey<Keeper>>);

    static_assert(std::is_constructible_v<PassKey<Keeper>, PassKey<Apprentice>>);
    static_assert(!std::is_constructible_v<PassKey<Apprentice>, PassKey<Keeper>>);
    static_assert(!std::is_constructible_v<PassKey<Keeper>, PassKey<Stranger>>);

    TEST(PassKeyTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(PassKeyTest, TheOwnerAndItsDescendantPassAKey) {
        Door door;
        Keeper::OpenWithBraces(door);
        Keeper::OpenWithNamedKey(door);
        Apprentice::OpenWithOwnKey(door);
        EXPECT_EQ(door.opened, 3);
    }

}  // namespace BECore::Tests
