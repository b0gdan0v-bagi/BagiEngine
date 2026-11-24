#include <BECore/Application/CommandLine.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Args = Vector<String>;

        CommandLine Make(const Args& args) {
            CommandLine line;
            line.Set(args);
            return line;
        }

    }  // namespace

    TEST(CommandLineTest, NoArgumentsFindsNothing) {
        const CommandLine line = Make({});
        EXPECT_TRUE(line.GetArguments().empty());
        EXPECT_FALSE(line.FindValue("--a").has_value());
        EXPECT_FALSE(line.HasFlag("--a"));
    }

    TEST(CommandLineTest, FindsTheValueOfNameEqualsValue) {
        const CommandLine line = Make({"--b", "--a=1", "--c=2"});
        ASSERT_TRUE(line.FindValue("--a").has_value());
        EXPECT_EQ(Require(line.FindValue("--a")), "1");
        EXPECT_EQ(Require(line.FindValue("--c")), "2");
        EXPECT_EQ(line.GetArguments().size(), 3u);
    }

    TEST(CommandLineTest, AnEmptyValueIsAValue) {
        const CommandLine line = Make({"--a="});
        ASSERT_TRUE(line.FindValue("--a").has_value());
        EXPECT_TRUE(Require(line.FindValue("--a")).empty());
        EXPECT_FALSE(line.HasFlag("--a"));
    }

    TEST(CommandLineTest, ABareNameIsAFlagAndHasNoValue) {
        const CommandLine line = Make({"--a"});
        EXPECT_TRUE(line.HasFlag("--a"));
        EXPECT_FALSE(line.FindValue("--a").has_value());
    }

    TEST(CommandLineTest, TheFirstMatchWinsWhenRepeated) {
        const CommandLine line = Make({"--a=first", "--a=second"});
        EXPECT_EQ(Require(line.FindValue("--a")), "first");
    }

    TEST(CommandLineTest, APrefixThatIsNotTheNameDoesNotMatch) {
        const CommandLine line = Make({"--ab=1", "--abc"});
        EXPECT_FALSE(line.FindValue("--a").has_value());
        EXPECT_FALSE(line.HasFlag("--a"));
        EXPECT_EQ(Require(line.FindValue("--ab")), "1");
    }

    TEST(CommandLineTest, TheValueKeepsEqualsSigns) {
        const CommandLine line = Make({"--a=x=y"});
        EXPECT_EQ(Require(line.FindValue("--a")), "x=y");
    }

    TEST(CommandLineTest, SetReplacesThePreviousArguments) {
        CommandLine line = Make({"--a=1"});
        line.Set(Args{"--b=2"});
        EXPECT_FALSE(line.FindValue("--a").has_value());
        EXPECT_EQ(Require(line.FindValue("--b")), "2");
    }

    TEST(CommandLineTest, FindTellsABareFlagFromAValue) {
        const CommandLine line = Make({"--bare", "--valued=7", "--empty="});
        const CommandLineArgument bare = Require(line.Find("--bare"));
        EXPECT_FALSE(bare.hasValue);
        const CommandLineArgument valued = Require(line.Find("--valued"));
        EXPECT_TRUE(valued.hasValue);
        EXPECT_EQ(valued.value, "7");
        const CommandLineArgument empty = Require(line.Find("--empty"));
        EXPECT_TRUE(empty.hasValue);
        EXPECT_TRUE(empty.value.empty());
        EXPECT_FALSE(line.Find("--absent").has_value());
    }

    TEST(CommandLineTest, FindTakesTheFirstFormOfAName) {
        const CommandLine bareFirst = Make({"--a", "--a=false"});
        EXPECT_FALSE(Require(bareFirst.Find("--a")).hasValue);
        const CommandLine valueFirst = Make({"--a=false", "--a"});
        EXPECT_TRUE(Require(valueFirst.Find("--a")).hasValue);
    }

    TEST(CommandLineTest, FindDoesNotMatchALongerName) {
        const CommandLine line = Make({"--control-portal=1", "--control-portal"});
        EXPECT_FALSE(line.Find("--control-port").has_value());
        EXPECT_FALSE(line.Find("--control").has_value());
    }

}  // namespace BECore::Tests
