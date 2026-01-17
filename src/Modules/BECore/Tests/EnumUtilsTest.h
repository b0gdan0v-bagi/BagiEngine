#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

   

}  // namespace BECore

namespace BECore::Tests {

     CORE_ENUM(TestColor, uint8_t, Red, Green, Blue, Yellow)

    /**
     * @brief Test for EnumUtils functionality
     */
    class EnumUtilsTest : public ITest {
        using ColorUtils = EnumUtils<TestColor>;

    public:
        EnumUtilsTest() = default;
        ~EnumUtilsTest() override = default;

        const char* GetName() const override {
            return "EnumUtilsTest";
        }

        bool Run() override {
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

            LOG_INFO("[EnumUtilsTest] All tests passed!");
            return true;
        }

    private:
        static void TestCompileTime() {
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

            LOG_INFO("[EnumUtilsTest] Compile-time tests OK");
        }

        void TestToString() {
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

        void TestToPoolString() {
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

        void TestFromString() {
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

        void TestFromPoolString() {
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

        void TestCast() {
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

        void TestCount() {
            ASSERT(ColorUtils::Count() == 4, "Count should be 4");

            LOG_INFO("[EnumUtilsTest] Count tests OK");
        }

        void TestArrays() {
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
    };

}  // namespace BECore::Tests
