#include <Common/Printers.h>
#include <fmt/format.h>

namespace BECore::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(fmt::is_formattable<int>::value, "int should be formattable");
    static_assert(fmt::is_formattable<float>::value, "float should be formattable");
    static_assert(fmt::is_formattable<double>::value, "double should be formattable");
    static_assert(fmt::is_formattable<bool>::value, "bool should be formattable");
    static_assert(fmt::is_formattable<const char*>::value, "const char* should be formattable");
    static_assert(fmt::is_formattable<eastl::string>::value, "eastl::string should be formattable");
    static_assert(fmt::is_formattable<StringView>::value, "eastl::string_view should be formattable");

    TEST(FormatTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    TEST(FormatTest, Basic) {
        EXPECT_EQ(BECore::Format("Hello, {}!", "World"), "Hello, World!");
    }

    TEST(FormatTest, Numeric) {
        EXPECT_EQ(BECore::Format("Value: {:03d}", 42), "Value: 042");
    }

    TEST(FormatTest, MultipleArguments) {
        EXPECT_EQ(BECore::Format("{} + {} = {}", 1, 1, 2), "1 + 1 = 2");
    }

    TEST(FormatTest, EastlTypes) {
        eastl::string name = "Bagi";
        StringView suffix = "Engine";
        EXPECT_EQ(BECore::Format("Welcome to {}{}", name, suffix), "Welcome to BagiEngine");
    }

    TEST(FormatTest, FormatTo) {
        eastl::fixed_string<char, 64> buffer;
        BECore::FormatTo(eastl::back_inserter(buffer), "Buffer: {}", 123);
        EXPECT_TRUE(buffer == StringView("Buffer: 123"));
    }

    TEST(FormatTest, UserDefinedLiteral) {
        using namespace BECore::Literals;

        EXPECT_EQ("Hello, {}!"_format("World"), "Hello, World!");
        EXPECT_EQ("Value: {}"_format(42), "Value: 42");
        EXPECT_EQ("{} + {} = {}"_format(2, 3, 5), "2 + 3 = 5");
        EXPECT_EQ("Formatted: {:03d}"_format(7), "Formatted: 007");

        eastl::string name = "Test";
        EXPECT_EQ("Name: {}"_format(name), "Name: Test");
    }

}  // namespace BECore::Tests
