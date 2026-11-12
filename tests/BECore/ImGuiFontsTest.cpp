#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <cmath>
#include <imgui.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

        /// A context of its own, with no backend: the atlas takes the fonts and nothing rasterizes them
        class ImGuiFontsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ImGui::CreateContext();
            }

            void TearDown() override {
                // The context goes first: its atlas reads the buffers
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            ImGuiFontBuffers buffers;
        };

    }  // namespace

    TEST(FontScaleForTest, TheUserScaleIsTheDisplayScaleOverThePixelDensity) {
        EXPECT_FLOAT_EQ(FontScaleFor(1.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(2.0f, 2.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(1.5f, 1.0f), 1.5f);
        EXPECT_FLOAT_EQ(FontScaleFor(1.0f, 2.0f), 0.5f);
    }

    TEST(FontScaleForTest, ANumberThatCannotBeAScaleGivesOne) {
        EXPECT_FLOAT_EQ(FontScaleFor(0.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(1.0f, 0.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(kNaN, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(1.0f, kNaN), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(-2.0f, 1.0f), 1.0f);
        EXPECT_FLOAT_EQ(FontScaleFor(std::numeric_limits<float>::infinity(), 1.0f), 1.0f);
    }

    TEST_F(ImGuiFontsTest, TheLoaderGivesTheTextFontWithIconsAndTheMonoFont) {
        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers);

        ASSERT_NE(fonts.ui, nullptr);
        ASSERT_NE(fonts.mono, nullptr);
        EXPECT_NE(fonts.ui, fonts.mono);
        EXPECT_EQ(fonts.ui->Sources.Size, 2);
        EXPECT_EQ(fonts.mono->Sources.Size, 1);
        EXPECT_FLOAT_EQ(fonts.ui->LegacySize, kUiFontSize);
        EXPECT_FLOAT_EQ(fonts.mono->LegacySize, kMonoFontSize);
        EXPECT_FALSE(buffers.ui.empty());
        EXPECT_FALSE(buffers.icons.empty());
        EXPECT_FALSE(buffers.mono.empty());
    }

    TEST_F(ImGuiFontsTest, TheTextFontIsTheDefaultOfTheContext) {
        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers);

        ASSERT_NE(fonts.ui, nullptr);
        EXPECT_EQ(ImGui::GetIO().Fonts->Fonts[0], fonts.ui);
    }

    TEST_F(ImGuiFontsTest, TheIconFontHasTheGlyphsOfTheHeaderAndTheTextFontHasCyrillic) {
        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers);

        ASSERT_NE(fonts.ui, nullptr);
        ASSERT_NE(fonts.mono, nullptr);
        EXPECT_TRUE(fonts.ui->IsGlyphInFont(static_cast<ImWchar>(ICON_MIN_LC)));
        EXPECT_TRUE(fonts.ui->IsGlyphInFont(static_cast<ImWchar>(ICON_MAX_16_LC)));
        EXPECT_TRUE(fonts.ui->IsGlyphInFont(static_cast<ImWchar>(0x0416)));
        EXPECT_TRUE(fonts.mono->IsGlyphInFont(static_cast<ImWchar>(0x0416)));
        EXPECT_FALSE(fonts.mono->IsGlyphInFont(static_cast<ImWchar>(ICON_MIN_LC)));
    }

    TEST_F(ImGuiFontsTest, AMissingTextFileLeavesNoTextFontAndNoCrash) {
        ImGuiFontPaths paths;
        paths.ui = "assets/fonts/NoSuchFont.ttf";

        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers, paths);

        EXPECT_EQ(fonts.ui, nullptr);
        EXPECT_NE(fonts.mono, nullptr);
        EXPECT_TRUE(buffers.ui.empty());
    }

    TEST_F(ImGuiFontsTest, AMissingIconFileLeavesTheTextFontWithOneSource) {
        ImGuiFontPaths paths;
        paths.icons = "assets/fonts/NoSuchIcons.ttf";

        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers, paths);

        ASSERT_NE(fonts.ui, nullptr);
        EXPECT_EQ(fonts.ui->Sources.Size, 1);
    }

    TEST_F(ImGuiFontsTest, AMissingMonoFileLeavesNoMonoFont) {
        ImGuiFontPaths paths;
        paths.mono = "assets/fonts/NoSuchMono.ttf";

        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers, paths);

        EXPECT_NE(fonts.ui, nullptr);
        EXPECT_EQ(fonts.mono, nullptr);
    }

    TEST_F(ImGuiFontsTest, TheMonoScopeDoesNothingWithoutAFont) {
        EXPECT_EQ(GetImGuiFonts().mono, nullptr);
        ScopedMonoFont scope;
        ScopedMonoFont explicitNull(nullptr);
    }

    TEST_F(ImGuiFontsTest, ActiveFontsAreSetAndCleared) {
        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers);
        SetActiveImGuiFonts(fonts);

        EXPECT_EQ(GetImGuiFonts().ui, fonts.ui);
        EXPECT_EQ(GetImGuiFonts().mono, fonts.mono);

        SetActiveImGuiFonts({});
        EXPECT_EQ(GetImGuiFonts().mono, nullptr);
    }

}  // namespace BECore::Tests
