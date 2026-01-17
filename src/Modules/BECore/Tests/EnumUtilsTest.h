#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Utils/EnumUtils.h>

namespace BECore {

    // Test enum for EnumUtils tests (must be in BECore namespace for CORE_ENUM macro)
    CORE_ENUM(TestColor, uint8_t, Red, Green, Blue, Yellow)

}  // namespace BECore

namespace BECore::Tests {

    /**
     * @brief Test for EnumUtils functionality
     */
    class EnumUtilsTest : public ITest {
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
            TestEnumToString();
            TestEnumToPoolString();
            TestStringToEnum();
            TestPoolStringToEnum();
            TestEnumCast();
            TestEnumCount();
            TestEnumArrays();

            LOG_INFO("[EnumUtilsTest] All tests passed!");
            return true;
        }

    private:
        static void TestCompileTime() {
            // EnumToString is constexpr
            static_assert(EnumToString(TestColor::Red) == "Red");
            static_assert(EnumToString(TestColor::Green) == "Green");
            static_assert(EnumToString(TestColor::Blue) == "Blue");
            static_assert(EnumToString(TestColor::Yellow) == "Yellow");

            // StringToEnum is constexpr
            static_assert(StringToEnum<TestColor>("Red") == TestColor::Red);
            static_assert(StringToEnum<TestColor>("Green") == TestColor::Green);
            static_assert(StringToEnum<TestColor>("Blue") == TestColor::Blue);
            static_assert(StringToEnum<TestColor>("Yellow") == TestColor::Yellow);

            // EnumCount is constexpr
            static_assert(EnumCount<TestColor>() == 4);

            // EnumNames returns constexpr array
            static_assert(EnumNames<TestColor>().size() == 4);
            static_assert(EnumNames<TestColor>()[0] == "Red");
            static_assert(EnumNames<TestColor>()[3] == "Yellow");

            // EnumValues returns constexpr array
            static_assert(EnumValues<TestColor>().size() == 4);
            static_assert(EnumValues<TestColor>()[0] == TestColor::Red);
            static_assert(EnumValues<TestColor>()[3] == TestColor::Yellow);

            // EnumCast is constexpr
            static_assert(EnumCast<TestColor>("Red").has_value());
            static_assert(EnumCast<TestColor>("Red").value() == TestColor::Red);
            static_assert(!EnumCast<TestColor>("InvalidColor").has_value());

            LOG_INFO("[EnumUtilsTest] Compile-time tests OK");
        }

        void TestEnumToString() {
            // Test EnumToString returns correct eastl::string_view
            ASSERT(EnumToString(TestColor::Red) == "Red", "EnumToString Red failed");
            ASSERT(EnumToString(TestColor::Green) == "Green", "EnumToString Green failed");
            ASSERT(EnumToString(TestColor::Blue) == "Blue", "EnumToString Blue failed");
            ASSERT(EnumToString(TestColor::Yellow) == "Yellow", "EnumToString Yellow failed");

            // Test with temporary
            auto sv = EnumToString(TestColor::Blue);
            ASSERT(sv.size() == 4, "EnumToString size mismatch");
            ASSERT(sv == "Blue", "EnumToString Blue mismatch");

            LOG_INFO("[EnumUtilsTest] EnumToString tests OK");
        }

        void TestEnumToPoolString() {
            // Test EnumToPoolString returns valid PoolString
            PoolString ps = EnumToPoolString(TestColor::Red);
            ASSERT(!ps.Empty(), "EnumToPoolString Red empty");
            ASSERT(ps.ToStringView() == "Red", "EnumToPoolString Red mismatch");

            // Test all values
            ASSERT(EnumToPoolString(TestColor::Green).ToStringView() == "Green");
            ASSERT(EnumToPoolString(TestColor::Blue).ToStringView() == "Blue");
            ASSERT(EnumToPoolString(TestColor::Yellow).ToStringView() == "Yellow");

            // Test that same enum value returns same PoolString (interned)
            PoolString ps1 = EnumToPoolString(TestColor::Red);
            PoolString ps2 = EnumToPoolString(TestColor::Red);
            ASSERT(ps1 == ps2, "EnumToPoolString should return same interned PoolString");

            // Verify it's actually the same pointer (interning works)
            ASSERT(ps1.CStr() == ps2.CStr(), "PoolString pointers should match");

            LOG_INFO("[EnumUtilsTest] EnumToPoolString tests OK");
        }

        void TestStringToEnum() {
            // Test StringToEnum with eastl::string_view
            ASSERT(StringToEnum<TestColor>(eastl::string_view{"Red"}) == TestColor::Red);
            ASSERT(StringToEnum<TestColor>(eastl::string_view{"Green"}) == TestColor::Green);
            ASSERT(StringToEnum<TestColor>(eastl::string_view{"Blue"}) == TestColor::Blue);
            ASSERT(StringToEnum<TestColor>(eastl::string_view{"Yellow"}) == TestColor::Yellow);

            // Test StringToEnum with std::string_view
            ASSERT(StringToEnum<TestColor>(std::string_view{"Red"}) == TestColor::Red);
            ASSERT(StringToEnum<TestColor>(std::string_view{"Green"}) == TestColor::Green);

            LOG_INFO("[EnumUtilsTest] StringToEnum tests OK");
        }

        void TestPoolStringToEnum() {
            // Test PoolStringToEnum
            PoolString psRed = PoolString::Intern("Red");
            PoolString psGreen = PoolString::Intern("Green");
            PoolString psBlue = PoolString::Intern("Blue");

            ASSERT(PoolStringToEnum<TestColor>(psRed) == TestColor::Red);
            ASSERT(PoolStringToEnum<TestColor>(psGreen) == TestColor::Green);
            ASSERT(PoolStringToEnum<TestColor>(psBlue) == TestColor::Blue);

            // Test round-trip: enum -> PoolString -> enum
            for (auto color : EnumValues<TestColor>()) {
                PoolString ps = EnumToPoolString(color);
                TestColor parsed = PoolStringToEnum<TestColor>(ps);
                ASSERT(parsed == color, "PoolString round-trip failed");
            }

            LOG_INFO("[EnumUtilsTest] PoolStringToEnum tests OK");
        }

        void TestEnumCast() {
            // Test EnumCast with valid values
            auto red = EnumCast<TestColor>("Red");
            ASSERT(red.has_value(), "EnumCast Red should have value");
            ASSERT(red.value() == TestColor::Red, "EnumCast Red mismatch");

            auto green = EnumCast<TestColor>(eastl::string_view{"Green"});
            ASSERT(green.has_value(), "EnumCast Green should have value");
            ASSERT(green.value() == TestColor::Green, "EnumCast Green mismatch");

            // Test EnumCast with invalid values
            auto invalid1 = EnumCast<TestColor>("InvalidColor");
            ASSERT(!invalid1.has_value(), "EnumCast InvalidColor should be nullopt");

            auto invalid2 = EnumCast<TestColor>("");
            ASSERT(!invalid2.has_value(), "EnumCast empty should be nullopt");

            auto invalid3 = EnumCast<TestColor>("red");  // case sensitive
            ASSERT(!invalid3.has_value(), "EnumCast lowercase should be nullopt");

            LOG_INFO("[EnumUtilsTest] EnumCast tests OK");
        }

        void TestEnumCount() {
            ASSERT(EnumCount<TestColor>() == 4, "EnumCount should be 4");

            LOG_INFO("[EnumUtilsTest] EnumCount tests OK");
        }

        void TestEnumArrays() {
            // Test EnumNames
            const auto& names = EnumNames<TestColor>();
            ASSERT(names.size() == 4, "EnumNames size mismatch");
            ASSERT(names[0] == "Red", "EnumNames[0] mismatch");
            ASSERT(names[1] == "Green", "EnumNames[1] mismatch");
            ASSERT(names[2] == "Blue", "EnumNames[2] mismatch");
            ASSERT(names[3] == "Yellow", "EnumNames[3] mismatch");

            // Test EnumValues
            const auto& values = EnumValues<TestColor>();
            ASSERT(values.size() == 4, "EnumValues size mismatch");
            ASSERT(values[0] == TestColor::Red, "EnumValues[0] mismatch");
            ASSERT(values[1] == TestColor::Green, "EnumValues[1] mismatch");
            ASSERT(values[2] == TestColor::Blue, "EnumValues[2] mismatch");
            ASSERT(values[3] == TestColor::Yellow, "EnumValues[3] mismatch");

            // Test EnumPoolStrings
            const auto& poolStrings = EnumPoolStrings<TestColor>();
            ASSERT(poolStrings.size() == 4, "EnumPoolStrings size mismatch");
            ASSERT(poolStrings[0].ToStringView() == "Red", "EnumPoolStrings[0] mismatch");
            ASSERT(poolStrings[1].ToStringView() == "Green", "EnumPoolStrings[1] mismatch");
            ASSERT(poolStrings[2].ToStringView() == "Blue", "EnumPoolStrings[2] mismatch");
            ASSERT(poolStrings[3].ToStringView() == "Yellow", "EnumPoolStrings[3] mismatch");

            // Verify pool strings are interned (same as EnumToPoolString)
            ASSERT(poolStrings[0] == EnumToPoolString(TestColor::Red));
            ASSERT(poolStrings[1] == EnumToPoolString(TestColor::Green));
            ASSERT(poolStrings[2] == EnumToPoolString(TestColor::Blue));
            ASSERT(poolStrings[3] == EnumToPoolString(TestColor::Yellow));

            LOG_INFO("[EnumUtilsTest] EnumArrays tests OK");
        }
    };

}  // namespace BECore::Tests
