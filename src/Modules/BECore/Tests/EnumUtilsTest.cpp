#include "EnumUtilsTest.h"

#include <BECore/Format/Format.h>
#include <BECore/Logger/Logger.h>

#include <Generated/EnumUtilsTest.gen.hpp>

namespace BECore::Tests {

    bool EnumUtilsTest::Run() {
        LOG_INFO("[EnumUtilsTest] Starting tests...");

        // --- Compile-time tests (static_assert) ---
        TestCompileTime();

        // --- Runtime tests ---
        TestToString();
        TestToPoolString();
        TestFromString();
        TestFromPoolString();
        TestCast();
        TestCount();
        TestArrays();
        TestFmtFormatting();

        LOG_INFO("[EnumUtilsTest] All tests passed!");
        return true;
    }

    void EnumUtilsTest::TestToString() {
        // Test ToString returns correct eastl::string_view
        ASSERT(ColorUtils::ToString(TestColor::Red) == "Red", "ToString Red failed");
        ASSERT(ColorUtils::ToString(TestColor::Green) == "Green", "ToString Green failed");
        ASSERT(ColorUtils::ToString(TestColor::Blue) == "Blue", "ToString Blue failed");
        ASSERT(ColorUtils::ToString(TestColor::Yellow) == "Yellow", "ToString Yellow failed");

        // Test with temporary
        auto sv = ColorUtils::ToString(TestColor::Blue);
        ASSERT(sv.size() == 4, "ToString size mismatch");
        ASSERT(sv == "Blue", "ToString Blue mismatch");

        LOG_INFO("[EnumUtilsTest] ToString tests OK");
    }

    void EnumUtilsTest::TestToPoolString() {
        // Test ToPoolString returns valid PoolString
        PoolString ps = ColorUtils::ToPoolString(TestColor::Red);
        ASSERT(!ps.Empty(), "ToPoolString Red empty");
        ASSERT(ps.ToStringView() == "Red", "ToPoolString Red mismatch");

        // Test all values
        ASSERT(ColorUtils::ToPoolString(TestColor::Green).ToStringView() == "Green");
        ASSERT(ColorUtils::ToPoolString(TestColor::Blue).ToStringView() == "Blue");
        ASSERT(ColorUtils::ToPoolString(TestColor::Yellow).ToStringView() == "Yellow");

        // Test that same enum value returns same PoolString (interned)
        PoolString ps1 = ColorUtils::ToPoolString(TestColor::Red);
        PoolString ps2 = ColorUtils::ToPoolString(TestColor::Red);
        ASSERT(ps1 == ps2, "ToPoolString should return same interned PoolString");

        // Verify it's actually the same pointer (interning works)
        ASSERT(ps1.CStr() == ps2.CStr(), "PoolString pointers should match");

        LOG_INFO("[EnumUtilsTest] ToPoolString tests OK");
    }

    void EnumUtilsTest::TestFromString() {
        // Test FromString with eastl::string_view
        ASSERT(ColorUtils::FromString(eastl::string_view{"Red"}) == TestColor::Red);
        ASSERT(ColorUtils::FromString(eastl::string_view{"Green"}) == TestColor::Green);
        ASSERT(ColorUtils::FromString(eastl::string_view{"Blue"}) == TestColor::Blue);
        ASSERT(ColorUtils::FromString(eastl::string_view{"Yellow"}) == TestColor::Yellow);

        // Test FromString with std::string_view
        ASSERT(ColorUtils::FromString(std::string_view{"Red"}) == TestColor::Red);
        ASSERT(ColorUtils::FromString(std::string_view{"Green"}) == TestColor::Green);

        // Test FromString with const char*
        ASSERT(ColorUtils::FromString("Red") == TestColor::Red);
        ASSERT(ColorUtils::FromString("Blue") == TestColor::Blue);

        LOG_INFO("[EnumUtilsTest] FromString tests OK");
    }

    void EnumUtilsTest::TestFromPoolString() {
        // Test FromPoolString
        PoolString psRed = PoolString::Intern("Red");
        PoolString psGreen = PoolString::Intern("Green");
        PoolString psBlue = PoolString::Intern("Blue");

        ASSERT(ColorUtils::FromPoolString(psRed) == TestColor::Red);
        ASSERT(ColorUtils::FromPoolString(psGreen) == TestColor::Green);
        ASSERT(ColorUtils::FromPoolString(psBlue) == TestColor::Blue);

        // Test round-trip: enum -> PoolString -> enum
        for (auto color : ColorUtils::Values()) {
            PoolString ps = ColorUtils::ToPoolString(color);
            TestColor parsed = ColorUtils::FromPoolString(ps);
            ASSERT(parsed == color, "PoolString round-trip failed");
        }

        LOG_INFO("[EnumUtilsTest] FromPoolString tests OK");
    }

    void EnumUtilsTest::TestCast() {
        // Test Cast with valid values
        auto red = ColorUtils::Cast("Red");
        ASSERT(red.has_value(), "Cast Red should have value");
        ASSERT(red.value() == TestColor::Red, "Cast Red mismatch");

        auto green = ColorUtils::Cast(eastl::string_view{"Green"});
        ASSERT(green.has_value(), "Cast Green should have value");
        ASSERT(green.value() == TestColor::Green, "Cast Green mismatch");

        // Test Cast with invalid values
        auto invalid1 = ColorUtils::Cast("InvalidColor");
        ASSERT(!invalid1.has_value(), "Cast InvalidColor should be nullopt");

        auto invalid2 = ColorUtils::Cast("");
        ASSERT(!invalid2.has_value(), "Cast empty should be nullopt");

        auto invalid3 = ColorUtils::Cast("red");  // case sensitive
        ASSERT(!invalid3.has_value(), "Cast lowercase should be nullopt");

        LOG_INFO("[EnumUtilsTest] Cast tests OK");
    }

    void EnumUtilsTest::TestCount() {
        ASSERT(ColorUtils::Count() == 4, "Count should be 4");

        LOG_INFO("[EnumUtilsTest] Count tests OK");
    }

    void EnumUtilsTest::TestArrays() {
        // Test Names
        const auto& names = ColorUtils::Names();
        ASSERT(names.size() == 4, "Names size mismatch");
        ASSERT(names[0] == "Red", "Names[0] mismatch");
        ASSERT(names[1] == "Green", "Names[1] mismatch");
        ASSERT(names[2] == "Blue", "Names[2] mismatch");
        ASSERT(names[3] == "Yellow", "Names[3] mismatch");

        // Test Values
        const auto& values = ColorUtils::Values();
        ASSERT(values.size() == 4, "Values size mismatch");
        ASSERT(values[0] == TestColor::Red, "Values[0] mismatch");
        ASSERT(values[1] == TestColor::Green, "Values[1] mismatch");
        ASSERT(values[2] == TestColor::Blue, "Values[2] mismatch");
        ASSERT(values[3] == TestColor::Yellow, "Values[3] mismatch");

        // Test PoolStrings
        const auto& poolStrings = ColorUtils::PoolStrings();
        ASSERT(poolStrings.size() == 4, "PoolStrings size mismatch");
        ASSERT(poolStrings[0].ToStringView() == "Red", "PoolStrings[0] mismatch");
        ASSERT(poolStrings[1].ToStringView() == "Green", "PoolStrings[1] mismatch");
        ASSERT(poolStrings[2].ToStringView() == "Blue", "PoolStrings[2] mismatch");
        ASSERT(poolStrings[3].ToStringView() == "Yellow", "PoolStrings[3] mismatch");

        // Verify pool strings are interned (same as ToPoolString)
        ASSERT(poolStrings[0] == ColorUtils::ToPoolString(TestColor::Red));
        ASSERT(poolStrings[1] == ColorUtils::ToPoolString(TestColor::Green));
        ASSERT(poolStrings[2] == ColorUtils::ToPoolString(TestColor::Blue));
        ASSERT(poolStrings[3] == ColorUtils::ToPoolString(TestColor::Yellow));

        LOG_INFO("[EnumUtilsTest] Arrays tests OK");
    }

    void EnumUtilsTest::TestFmtFormatting() {
        // Test basic fmt formatting
        eastl::string s1 = Format("Color: {}", TestColor::Red);
        ASSERT(s1 == "Color: Red", "fmt formatting Red failed");

        eastl::string s2 = Format("Color: {}", TestColor::Green);
        ASSERT(s2 == "Color: Green", "fmt formatting Green failed");

        eastl::string s3 = Format("Color: {}", TestColor::Blue);
        ASSERT(s3 == "Color: Blue", "fmt formatting Blue failed");

        eastl::string s4 = Format("Color: {}", TestColor::Yellow);
        ASSERT(s4 == "Color: Yellow", "fmt formatting Yellow failed");

        // Test multiple enums in one format string
        eastl::string s5 = Format("Colors: {}, {}, {}", TestColor::Red, TestColor::Green, TestColor::Blue);
        ASSERT(s5 == "Colors: Red, Green, Blue", "fmt formatting multiple enums failed");

        // Test mixed formatting with other types
        eastl::string s6 = Format("Color {} at index {}", TestColor::Yellow, 3);
        ASSERT(s6 == "Color Yellow at index 3", "fmt formatting mixed types failed");

        // Test with temporary enum value
        eastl::string s7 = Format("Selected: {}", static_cast<TestColor>(2));  // Blue
        ASSERT(s7 == "Selected: Blue", "fmt formatting temporary enum failed");

        LOG_INFO("[EnumUtilsTest] fmt formatting tests OK");
    }

}  // namespace BECore::Tests
