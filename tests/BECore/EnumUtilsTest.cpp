#include <Common/Printers.h>

namespace BECore::Tests {

    CORE_ENUM(TestColor, uint8_t, Red, Green, Blue, Yellow)

    using ColorUtils = EnumUtils<TestColor>;

    // Compile-time checks: a regression breaks the build.

    // ToString is constexpr
    static_assert(ColorUtils::ToString(TestColor::Red) == "Red");
    static_assert(ColorUtils::ToString(TestColor::Green) == "Green");
    static_assert(ColorUtils::ToString(TestColor::Blue) == "Blue");
    static_assert(ColorUtils::ToString(TestColor::Yellow) == "Yellow");

    // FromString is constexpr
    static_assert(ColorUtils::FromString("Red") == TestColor::Red);
    static_assert(ColorUtils::FromString("Green") == TestColor::Green);
    static_assert(ColorUtils::FromString("Blue") == TestColor::Blue);
    static_assert(ColorUtils::FromString("Yellow") == TestColor::Yellow);

    // Count is constexpr
    static_assert(ColorUtils::Count() == 4);

    // Names returns constexpr array
    static_assert(ColorUtils::Names().size() == 4);
    static_assert(ColorUtils::Names()[0] == "Red");
    static_assert(ColorUtils::Names()[3] == "Yellow");

    // Values returns constexpr array
    static_assert(ColorUtils::Values().size() == 4);
    static_assert(ColorUtils::Values()[0] == TestColor::Red);
    static_assert(ColorUtils::Values()[3] == TestColor::Yellow);

    // Cast is constexpr
    static_assert(ColorUtils::Cast("Red").has_value());
    static_assert(ColorUtils::Cast("Red").value() == TestColor::Red);
    static_assert(!ColorUtils::Cast("InvalidColor").has_value());

    TEST(EnumUtilsTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(EnumUtilsTest, ToString) {
        EXPECT_EQ(ColorUtils::ToString(TestColor::Red), "Red");
        EXPECT_EQ(ColorUtils::ToString(TestColor::Green), "Green");
        EXPECT_EQ(ColorUtils::ToString(TestColor::Blue), "Blue");
        EXPECT_EQ(ColorUtils::ToString(TestColor::Yellow), "Yellow");

        auto sv = ColorUtils::ToString(TestColor::Blue);
        EXPECT_EQ(sv.size(), 4u);
        EXPECT_EQ(sv, "Blue");
    }

    TEST(EnumUtilsTest, ToPoolString) {
        PoolString ps = ColorUtils::ToPoolString(TestColor::Red);
        EXPECT_FALSE(ps.Empty());
        EXPECT_EQ(ps.ToStringView(), "Red");

        EXPECT_EQ(ColorUtils::ToPoolString(TestColor::Green).ToStringView(), "Green");
        EXPECT_EQ(ColorUtils::ToPoolString(TestColor::Blue).ToStringView(), "Blue");
        EXPECT_EQ(ColorUtils::ToPoolString(TestColor::Yellow).ToStringView(), "Yellow");

        // The same enum value must give the same interned PoolString
        PoolString ps1 = ColorUtils::ToPoolString(TestColor::Red);
        PoolString ps2 = ColorUtils::ToPoolString(TestColor::Red);
        EXPECT_EQ(ps1, ps2);
        EXPECT_EQ(ps1.CStr(), ps2.CStr()) << "Interning must return the same pointer";
    }

    TEST(EnumUtilsTest, FromString) {
        EXPECT_EQ(ColorUtils::FromString(StringView{"Red"}), TestColor::Red);
        EXPECT_EQ(ColorUtils::FromString(StringView{"Green"}), TestColor::Green);
        EXPECT_EQ(ColorUtils::FromString(StringView{"Blue"}), TestColor::Blue);
        EXPECT_EQ(ColorUtils::FromString(StringView{"Yellow"}), TestColor::Yellow);

        EXPECT_EQ(ColorUtils::FromString(std::string_view{"Red"}), TestColor::Red);
        EXPECT_EQ(ColorUtils::FromString(std::string_view{"Green"}), TestColor::Green);

        EXPECT_EQ(ColorUtils::FromString("Red"), TestColor::Red);
        EXPECT_EQ(ColorUtils::FromString("Blue"), TestColor::Blue);
    }

    TEST(EnumUtilsTest, FromPoolString) {
        EXPECT_EQ(ColorUtils::FromPoolString(PoolString::Intern("Red")), TestColor::Red);
        EXPECT_EQ(ColorUtils::FromPoolString(PoolString::Intern("Green")), TestColor::Green);
        EXPECT_EQ(ColorUtils::FromPoolString(PoolString::Intern("Blue")), TestColor::Blue);

        // Round-trip: enum -> PoolString -> enum
        for (auto color : ColorUtils::Values()) {
            PoolString ps = ColorUtils::ToPoolString(color);
            EXPECT_EQ(ColorUtils::FromPoolString(ps), color) << "Round-trip failed for " << ps.CStr();
        }
    }

    TEST(EnumUtilsTest, Cast) {
        auto red = ColorUtils::Cast("Red");
        ASSERT_TRUE(red.has_value());
        EXPECT_EQ(red.value(), TestColor::Red);

        auto green = ColorUtils::Cast(StringView{"Green"});
        ASSERT_TRUE(green.has_value());
        EXPECT_EQ(green.value(), TestColor::Green);

        EXPECT_FALSE(ColorUtils::Cast("InvalidColor").has_value());
        EXPECT_FALSE(ColorUtils::Cast("").has_value());
        EXPECT_FALSE(ColorUtils::Cast("red").has_value()) << "Cast is case sensitive";
    }

    TEST(EnumUtilsTest, Count) {
        EXPECT_EQ(ColorUtils::Count(), 4u);
    }

    TEST(EnumUtilsTest, Arrays) {
        const auto& names = ColorUtils::Names();
        ASSERT_EQ(names.size(), 4u);
        EXPECT_EQ(names[0], "Red");
        EXPECT_EQ(names[1], "Green");
        EXPECT_EQ(names[2], "Blue");
        EXPECT_EQ(names[3], "Yellow");

        const auto& values = ColorUtils::Values();
        ASSERT_EQ(values.size(), 4u);
        EXPECT_EQ(values[0], TestColor::Red);
        EXPECT_EQ(values[1], TestColor::Green);
        EXPECT_EQ(values[2], TestColor::Blue);
        EXPECT_EQ(values[3], TestColor::Yellow);

        const auto& poolStrings = ColorUtils::PoolStrings();
        ASSERT_EQ(poolStrings.size(), 4u);
        EXPECT_EQ(poolStrings[0].ToStringView(), "Red");
        EXPECT_EQ(poolStrings[1].ToStringView(), "Green");
        EXPECT_EQ(poolStrings[2].ToStringView(), "Blue");
        EXPECT_EQ(poolStrings[3].ToStringView(), "Yellow");

        // Pool strings are interned (the same as ToPoolString)
        EXPECT_EQ(poolStrings[0], ColorUtils::ToPoolString(TestColor::Red));
        EXPECT_EQ(poolStrings[1], ColorUtils::ToPoolString(TestColor::Green));
        EXPECT_EQ(poolStrings[2], ColorUtils::ToPoolString(TestColor::Blue));
        EXPECT_EQ(poolStrings[3], ColorUtils::ToPoolString(TestColor::Yellow));
    }

    TEST(EnumUtilsTest, FmtFormatting) {
        EXPECT_EQ(Format("Color: {}", TestColor::Red), "Color: Red");
        EXPECT_EQ(Format("Color: {}", TestColor::Green), "Color: Green");
        EXPECT_EQ(Format("Color: {}", TestColor::Blue), "Color: Blue");
        EXPECT_EQ(Format("Color: {}", TestColor::Yellow), "Color: Yellow");

        EXPECT_EQ(Format("Colors: {}, {}, {}", TestColor::Red, TestColor::Green, TestColor::Blue), "Colors: Red, Green, Blue");
        EXPECT_EQ(Format("Color {} at index {}", TestColor::Yellow, 3), "Color Yellow at index 3");
        EXPECT_EQ(Format("Selected: {}", static_cast<TestColor>(2)), "Selected: Blue");
    }

}  // namespace BECore::Tests
