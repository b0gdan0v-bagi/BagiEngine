#pragma once

#include <BECore/Tests/ITest.h>
#include <BECore/Format/Format.h>
#include <BECore/PoolString/PoolString.h>
#include <fmt/format.h>

namespace BECore::Tests {

    /**
     * @brief Test for BECore::Format functionality
     */
    class FormatTest : public ITest {
        BE_CLASS(FormatTest)
    public:
        FormatTest() = default;
        ~FormatTest() override = default;

        bool Run() override;
        eastl::string_view GetName() override {
            return GetStaticTypeName();
        }

    private:
        /**
         * @brief Compile-time tests using static_assert
         */
        static constexpr void TestCompileTime() {
            // Verify primitive types are formattable
            static_assert(fmt::is_formattable<int>::value, "int should be formattable");
            static_assert(fmt::is_formattable<float>::value, "float should be formattable");
            static_assert(fmt::is_formattable<double>::value, "double should be formattable");
            static_assert(fmt::is_formattable<bool>::value, "bool should be formattable");
            static_assert(fmt::is_formattable<const char*>::value, "const char* should be formattable");

            // Verify EASTL types are formattable (via custom formatters)
            static_assert(fmt::is_formattable<eastl::string>::value, "eastl::string should be formattable");
            static_assert(fmt::is_formattable<eastl::string_view>::value, "eastl::string_view should be formattable");
        }
    };

    // Compile-time validation of test class
    static_assert(ValidTest<FormatTest>);

}  // namespace BECore::Tests
