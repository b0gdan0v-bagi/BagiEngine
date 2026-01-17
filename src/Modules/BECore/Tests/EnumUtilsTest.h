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

        const char* GetName() const override;
        bool Run() override;

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

        void TestToString();
        void TestToPoolString();
        void TestFromString();
        void TestFromPoolString();
        void TestCast();
        void TestCount();
        void TestArrays();
        void TestFmtFormatting();
    };

}  // namespace BECore::Tests
