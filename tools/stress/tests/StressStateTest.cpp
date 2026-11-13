#include <Stress/StressState.h>
#include <gtest/gtest.h>

namespace BECore::Stress::Tests {

    TEST(StressStateTest, StartsFromTheSettings) {
        StressSettings settings;
        settings.scene = StressScene::Ui;
        settings.count = 300;
        settings.language = StressLanguage::Ru;
        const StressState state(settings);
        EXPECT_EQ(state.GetScene(), StressScene::Ui);
        EXPECT_EQ(state.GetCount(), 300u);
        EXPECT_EQ(state.GetLanguage(), StressLanguage::Ru);
    }

    TEST(StressStateTest, TheDefaultStateIsTheDefaultSettings) {
        const StressState state;
        EXPECT_EQ(state.GetScene(), StressScene::Sprites);
        EXPECT_EQ(state.GetCount(), kDefaultCount);
        EXPECT_EQ(state.GetLanguage(), StressLanguage::En);
        EXPECT_GE(state.Version(), 1u);
    }

    TEST(StressStateTest, TheVersionGrowsWithEveryChange) {
        StressState state;
        uint64_t version = state.Version();

        state.SetScene(StressScene::Entities);
        EXPECT_GT(state.Version(), version);
        version = state.Version();

        state.SetCount(10);
        EXPECT_GT(state.Version(), version);
        version = state.Version();

        state.SetLanguage(StressLanguage::Ru);
        EXPECT_GT(state.Version(), version);
        EXPECT_EQ(state.GetScene(), StressScene::Entities);
        EXPECT_EQ(state.GetCount(), 10u);
        EXPECT_EQ(state.GetLanguage(), StressLanguage::Ru);
    }

    TEST(StressStateTest, AnEqualValueChangesNothing) {
        StressState state;
        state.SetCount(7);
        const uint64_t version = state.Version();
        state.SetScene(state.GetScene());
        state.SetCount(7);
        state.SetLanguage(state.GetLanguage());
        EXPECT_EQ(state.Version(), version);
    }

    TEST(StressStateTest, TheVersionNeverGoesBackWhenAValueReturns) {
        StressState state;
        const uint64_t first = state.Version();
        state.SetScene(StressScene::Ui);
        state.SetScene(StressScene::Sprites);
        EXPECT_GT(state.Version(), first + 1);
    }

    TEST(StressStateTest, ACountAboveTheMaximumIsClamped) {
        StressState state;
        state.SetCount(kMaxCount + 1);
        EXPECT_EQ(state.GetCount(), kMaxCount);
        const uint64_t version = state.Version();
        state.SetCount(0xFFFFFFFFu);
        EXPECT_EQ(state.GetCount(), kMaxCount);
        EXPECT_EQ(state.Version(), version);
    }

    TEST(StressStateTest, ACountOfZeroIsAllowed) {
        StressState state;
        state.SetCount(0);
        EXPECT_EQ(state.GetCount(), 0u);
    }

}  // namespace BECore::Stress::Tests
