#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    /// A type formatted through Formatter<T> below, the way a module teaches the layer a type of its own.
    struct FormatPoint {
        int x = 0;
        int y = 0;
    };

    struct NotFormattable {};

}  // namespace BECore::Tests

namespace BECore {

    template <>
    struct Formatter<Tests::FormatPoint> : TextFormatter {
        FormatContext::iterator Format(const Tests::FormatPoint& point, FormatContext& ctx) const {
            const String text = BECore::Format("({},{})", point.x, point.y);
            return FormatText(text, ctx);
        }
    };

}  // namespace BECore

namespace BECore::Tests {

    // Compile-time checks: a regression breaks the build.
    static_assert(Formattable<int>, "int should be formattable");
    static_assert(Formattable<float>, "float should be formattable");
    static_assert(Formattable<double>, "double should be formattable");
    static_assert(Formattable<bool>, "bool should be formattable");
    static_assert(Formattable<const char*>, "const char* should be formattable");
    static_assert(Formattable<String>, "eastl::string should be formattable");
    static_assert(Formattable<StringView>, "eastl::string_view should be formattable");
    static_assert(Formattable<FormatPoint>, "a type with a Formatter<T> should be formattable");
    static_assert(!Formattable<NotFormattable>, "a type without a formatter is not formattable");
    static_assert(HasFormatter<FormatPoint> && !HasFormatter<int> && !HasFormatter<NotFormattable>);
    // The layer's concept says what fmt says (the one place that names fmt::is_formattable besides the layer)
    static_assert(Formattable<FormatPoint> == fmt::is_formattable<FormatPoint>::value);        //-NoStd: tests the layer against fmt itself
    static_assert(Formattable<NotFormattable> == fmt::is_formattable<NotFormattable>::value);  //-NoStd: tests the layer against fmt itself

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
        String name = "Bagi";
        StringView suffix = "Engine";
        EXPECT_EQ(BECore::Format("Welcome to {}{}", name, suffix), "Welcome to BagiEngine");
    }

    TEST(FormatTest, FormatTo) {
        FixedString<64> buffer;
        BECore::FormatTo(eastl::back_inserter(buffer), "Buffer: {}", 123);
        EXPECT_TRUE(buffer == StringView("Buffer: 123"));
    }

    TEST(FormatTest, FormatterOfATypeFormatsWithAndWithoutASpec) {
        const FormatPoint point{3, -4};
        EXPECT_EQ(BECore::Format("{}", point), "(3,-4)");
        EXPECT_EQ(BECore::Format("[{:>8}]", point), "[  (3,-4)]");
        EXPECT_EQ(BECore::Format("[{:<6}|]", FormatPoint{1, 2}), "[(1,2) |]");
        EXPECT_EQ(BECore::Format("{} and {}", point, 7), "(3,-4) and 7");
    }

    TEST(FormatTest, FormatArgNamesAnArgument) {
        EXPECT_EQ(BECore::Format("{a}-{b}", FormatArg("a", 1), FormatArg("b", "x")), "1-x");
        EXPECT_EQ(BECore::Format("{a}{a}", FormatArg("a", 5)), "55");

        const String text = "owned";
        EXPECT_EQ(BECore::Format("{t}!", FormatArg("t", text.c_str())), "owned!");
    }

    TEST(FormatTest, RuntimeFormatTakesAStringThatIsNotALiteral) {
        const String pattern = "{}+{}";
        EXPECT_EQ(BECore::Format(RuntimeFormat(pattern), 1, 2), "1+2");

        const String named = "{x}/{x}";
        EXPECT_EQ(BECore::Format(RuntimeFormat(named), FormatArg("x", 9)), "9/9");
    }

    TEST(FormatTest, FormatToNCutsAtTheBufferAndAllocatesNothing) {
        constexpr size_t kCapacity = 16;
        char buffer[kCapacity];
        std::fill(std::begin(buffer), std::end(buffer), '#');

        const AllocTracker::ThreadCounters before = AllocTracker::GetThreadCounters();
        const FormatResult fits = FormatToN(buffer, kCapacity, "{}-{}", 12, 34);
        const AllocTracker::ThreadCounters after = AllocTracker::GetThreadCounters();
        if constexpr (AllocTracker::kEnabled) {
            EXPECT_EQ(after.count, before.count);
        }

        EXPECT_EQ(fits.size, 5U);
        EXPECT_FALSE(fits.truncated);
        EXPECT_EQ(StringView(buffer, fits.size), "12-34");

        constexpr StringView kLong = "0123456789abcdefXYZ";
        const FormatResult cut = FormatToN(buffer, kCapacity, "{}", kLong);
        EXPECT_EQ(cut.size, kLong.size());
        EXPECT_TRUE(cut.truncated);
        EXPECT_EQ(StringView(buffer, kCapacity), kLong.substr(0, kCapacity));

        // Nothing is written past the capacity: format into the first half of a larger array and check the rest
        char wide[kCapacity * 2];
        std::fill(std::begin(wide), std::end(wide), '#');
        const FormatResult half = FormatToN(wide, kCapacity, "{}", kLong);
        EXPECT_TRUE(half.truncated);
        for (size_t i = kCapacity; i < kCapacity * 2; ++i) {
            EXPECT_EQ(wide[i], '#') << i;
        }
    }

    TEST(FormatTest, PrintWritesIntoACStream) {
        std::FILE* file = std::tmpfile();
        ASSERT_NE(file, nullptr);

        Print(file, "{}:{:03d}", "id", 7);
        Print(file, "|{}", FormatPoint{1, 2});
        std::fflush(file);
        std::rewind(file);

        char text[64] = {};
        const size_t read = std::fread(text, 1, sizeof(text) - 1, file);
        std::fclose(file);
        EXPECT_EQ(StringView(text, read), "id:007|(1,2)");
    }

    TEST(FormatTest, UserDefinedLiteral) {
        using namespace BECore::Literals;

        EXPECT_EQ("Hello, {}!"_format("World"), "Hello, World!");
        EXPECT_EQ("Value: {}"_format(42), "Value: 42");
        EXPECT_EQ("{} + {} = {}"_format(2, 3, 5), "2 + 3 = 5");
        EXPECT_EQ("Formatted: {:03d}"_format(7), "Formatted: 007");

        String name = "Test";
        EXPECT_EQ("Name: {}"_format(name), "Name: Test");
    }

}  // namespace BECore::Tests
