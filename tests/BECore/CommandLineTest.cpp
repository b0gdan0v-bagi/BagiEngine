#include <BECore/Application/CommandLine.h>
#include <BECore/GameManager/CoreManager.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using Args = eastl::vector<String>;

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
        EXPECT_EQ(*line.FindValue("--a"), "1");
        EXPECT_EQ(*line.FindValue("--c"), "2");
        EXPECT_EQ(line.GetArguments().size(), 3u);
    }

    TEST(CommandLineTest, AnEmptyValueIsAValue) {
        const CommandLine line = Make({"--a="});
        ASSERT_TRUE(line.FindValue("--a").has_value());
        EXPECT_TRUE(line.FindValue("--a")->empty());
        EXPECT_FALSE(line.HasFlag("--a"));
    }

    TEST(CommandLineTest, ABareNameIsAFlagAndHasNoValue) {
        const CommandLine line = Make({"--a"});
        EXPECT_TRUE(line.HasFlag("--a"));
        EXPECT_FALSE(line.FindValue("--a").has_value());
    }

    TEST(CommandLineTest, TheFirstMatchWinsWhenRepeated) {
        const CommandLine line = Make({"--a=first", "--a=second"});
        EXPECT_EQ(*line.FindValue("--a"), "first");
    }

    TEST(CommandLineTest, APrefixThatIsNotTheNameDoesNotMatch) {
        const CommandLine line = Make({"--ab=1", "--abc"});
        EXPECT_FALSE(line.FindValue("--a").has_value());
        EXPECT_FALSE(line.HasFlag("--a"));
        EXPECT_EQ(*line.FindValue("--ab"), "1");
    }

    TEST(CommandLineTest, TheValueKeepsEqualsSigns) {
        const CommandLine line = Make({"--a=x=y"});
        EXPECT_EQ(*line.FindValue("--a"), "x=y");
    }

    TEST(CommandLineTest, SetReplacesThePreviousArguments) {
        CommandLine line = Make({"--a=1"});
        line.Set(Args{"--b=2"});
        EXPECT_FALSE(line.FindValue("--a").has_value());
        EXPECT_EQ(*line.FindValue("--b"), "2");
    }

    TEST(CommandLineTest, CoreManagerHoldsOneInstance) {
        EXPECT_EQ(&CoreManager::GetCommandLine(), &CoreManager::GetCommandLine());
    }

}  // namespace BECore::Tests
