#include <BECore/GuideCppRulesExample.h>
#include <Common/AllocCheck.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using namespace GuideCppRulesExample;

        constexpr int32_t kSteadyFrames = 100;

    }  // namespace

    TEST(GuideCppRulesExampleTest, AWalletIsAskedWithAViewAndAnswersWithAnOptional) {
        Wallet wallet;
        wallet.Set("mira", 12);

        const StringView asked = "mira";
        EXPECT_EQ(Require(wallet.Find(asked)), 12);
        EXPECT_EQ(wallet.Find("nobody"), std::nullopt);
    }

    TEST(GuideCppRulesExampleTest, ANameFromDataEqualsTheConstantOfTheSameTextAndNoOther) {
        EXPECT_TRUE(IsLevelField("level"));
        EXPECT_FALSE(IsLevelField("coins"));
        const String fromAFile = "coins";
        EXPECT_EQ(PoolString::Intern(fromAFile), Ids::coins);
    }

    TEST(GuideCppRulesExampleTest, TwoOwnersShareOneObjectAndItDiesWithTheLastOne) {
        bool destroyed = false;
        IntrusivePtr<Treasure> treasure = DigUp(destroyed);
        auto pouch = MakeUnique<Pouch>(treasure);
        EXPECT_EQ(treasure->GetRefCount(), 2);

        treasure.Reset();
        EXPECT_FALSE(destroyed) << "the pouch still owns it";

        pouch.reset();
        EXPECT_TRUE(destroyed);
    }

    TEST(GuideCppRulesExampleTest, ALevelIsReadOrRefusedWithTheReasonAsAValue) {
        EXPECT_EQ(Require(ParseLevel("7")), 7);
        EXPECT_EQ(ParseLevel("").error(), ParseError::Empty);
        EXPECT_EQ(ParseLevel("seven").error(), ParseError::NotANumber);
        EXPECT_EQ(ParseLevel("51").error(), ParseError::OutOfRange);
        EXPECT_EQ(ParseLevel("0").error(), ParseError::OutOfRange);
    }

    TEST(GuideCppRulesExampleTest, ABadLevelIsLoggedByTheNameOfItsErrorAndTheGameStartsFromTheFirstLevel) {
        const LogRecorder log;

        EXPECT_EQ(LevelOrFirst("seven"), kFirstLevel);
        EXPECT_EQ(LevelOrFirst("12"), 12);
        LogEvent::Flush();

        EXPECT_EQ(log.Count(LogLevel::Warning, {"Cannot read level 'seven': NotANumber"}), 1u);
        EXPECT_EQ(log.Count(LogLevel::Warning, {"12"}), 0u) << "a good level logs nothing";
    }

    TEST(GuideCppRulesExampleTest, AFormatAndAnAppendGiveTheSameText) {
        String appended;
        AppendCoins(appended, "mira", 12);

        EXPECT_EQ(DescribeCoins("mira", 12), "mira has 12 coins");
        EXPECT_EQ(appended, DescribeCoins("mira", 12));
    }

    TEST(GuideCppRulesExampleTest, APercentIsWholeNumbersOfAPositiveWhole) {
        EXPECT_EQ(PercentOf(1, 4), 25);
    }

    TEST(GuideCppRulesExampleTest, ALabelRebuildsItsTextOnlyWhenTheScoreMovedAndAnUnchangedScoreAllocatesNothing) {
        ScoreLabel label;
        const String& first = label.Text(7);
        EXPECT_EQ(first, "Your best score so far: 7");

        ExpectNoAllocations([&] {
            for (int32_t frame = 0; frame < kSteadyFrames; ++frame) {
                EXPECT_EQ(&label.Text(7), &first) << "the same member, not a new text";
            }
        });

        // The check above passes for a counter that never counts: a moved score must show as an allocation
        if constexpr (AllocTracker::kEnabled) {
            const AllocCount moved = CountAllocations([&] { EXPECT_EQ(label.Text(8), "Your best score so far: 8"); });
            EXPECT_GT(moved.count, 0u) << "a moved score builds a new text, and the tracker must count it";
        } else {
            EXPECT_EQ(label.Text(8), "Your best score so far: 8");
        }
    }

}  // namespace BECore::Tests
