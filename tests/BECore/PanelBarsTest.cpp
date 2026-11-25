#include <Common/EngineEnvironment.h>
#include <Widgets/PanelAssets/PanelBars.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        class CountingBar : public IPanelBar {
        public:
            void DrawBar() override {
                ++draws;
            }

            int draws = 0;
        };

    }  // namespace

    /// The registry a C++ window finds the bar of an asset in
    class PanelBarsTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            PanelBarsRegistry::DestroyInstance();
        }

        void TearDown() override {
            PanelBarsRegistry::DestroyInstance();
        }

        static PanelBarsRegistry& Registry() {
            return PanelBarsRegistry::GetInstance();
        }
    };

    TEST_F(PanelBarsTest, ARegisteredBarIsDrawnByNameAndTheHandleEndsIt) {
        CountingBar bar;
        {
            const PanelBarHandle handle = Registry().Register("Toolbar", bar);
            EXPECT_TRUE(handle);
            EXPECT_TRUE(PanelBarsRegistry::Draw("Toolbar"));
            EXPECT_EQ(bar.draws, 1);
        }
        EXPECT_FALSE(PanelBarsRegistry::Draw("Toolbar"));
        EXPECT_EQ(bar.draws, 1);
    }

    TEST_F(PanelBarsTest, AnUnknownNameDrawsNothingAndAnUnbornRegistryToo) {
        EXPECT_FALSE(PanelBarsRegistry::Draw("Nobody")) << "no registry yet";
        EXPECT_FALSE(PanelBarsRegistry::IsInstanceCreated()) << "a draw made the registry";

        CountingBar bar;
        const PanelBarHandle handle = Registry().Register("Toolbar", bar);
        EXPECT_FALSE(PanelBarsRegistry::Draw("Nobody"));
        EXPECT_FALSE(PanelBarsRegistry::Draw("toolbar")) << "names are case sensitive";
        EXPECT_EQ(bar.draws, 0);
    }

    TEST_F(PanelBarsTest, ASecondRegistrationOfANameReplacesTheFirstAndTheOrphanedHandleLeavesItAlone) {
        CountingBar first;
        CountingBar second;
        PanelBarHandle firstHandle = Registry().Register("Menu", first);
        const PanelBarHandle secondHandle = Registry().Register("Menu", second);
        EXPECT_TRUE(PanelBarsRegistry::Draw("Menu"));
        EXPECT_EQ(first.draws, 0);
        EXPECT_EQ(second.draws, 1);

        firstHandle.Reset();
        EXPECT_TRUE(PanelBarsRegistry::Draw("Menu")) << "the orphaned handle unregistered the newer bar";
        EXPECT_EQ(second.draws, 2);
    }

    TEST_F(PanelBarsTest, TwoNamesAreTwoBarsAndAMovedHandleKeepsItsRegistration) {
        CountingBar menu;
        CountingBar toolbar;
        PanelBarHandle moved;
        const PanelBarHandle toolbarHandle = Registry().Register("Toolbar", toolbar);
        {
            PanelBarHandle handle = Registry().Register("Menu", menu);
            moved = std::move(handle);
            EXPECT_FALSE(handle);
        }
        EXPECT_TRUE(PanelBarsRegistry::Draw("Menu"));
        EXPECT_EQ(menu.draws, 1);
        EXPECT_EQ(toolbar.draws, 0);

        moved.Reset();
        EXPECT_FALSE(PanelBarsRegistry::Draw("Menu"));
        EXPECT_TRUE(PanelBarsRegistry::Draw("Toolbar"));
    }

    TEST_F(PanelBarsTest, AHandleThatOutlivesTheRegistryEndsQuietly) {
        CountingBar bar;
        PanelBarHandle handle = Registry().Register("Toolbar", bar);
        PanelBarsRegistry::DestroyInstance();
        handle.Reset();
        EXPECT_FALSE(PanelBarsRegistry::Draw("Toolbar"));
    }

}  // namespace BECore::Tests
