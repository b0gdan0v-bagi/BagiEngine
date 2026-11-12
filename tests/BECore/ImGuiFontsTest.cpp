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
        /// The order of the sources of the UI font: Inter first, Lucide merged after it
        constexpr unsigned int kTextSourceIdx = 0;
        constexpr unsigned int kIconSourceIdx = 1;

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

    TEST_F(ImGuiFontsTest, EveryLucideCodepointOfTheHeaderIsDrawnFromTheIconSource) {
        ImGuiIO& io = ImGui::GetIO();
        const ImGuiFonts fonts = LoadImGuiFonts(io, buffers);

        ASSERT_NE(fonts.ui, nullptr);
        ASSERT_NE(fonts.mono, nullptr);

        // A font of its own with only the icon file says which codepoints Lucide has
        ImFontConfig iconOnlyConfig;
        iconOnlyConfig.FontDataOwnedByAtlas = false;
        ImFont* iconOnly = io.Fonts->AddFontFromMemoryTTF(buffers.icons.data(), static_cast<int>(buffers.icons.size()), kUiFontSize, &iconOnlyConfig);
        ASSERT_NE(iconOnly, nullptr);

        // Inter has glyphs of its own in the private use area, on the codepoints of Lucide: the first source that has a
        // codepoint wins, so the merge must keep Inter away from the span
        ImFontBaked* baked = fonts.ui->GetFontBaked(kUiFontSize);
        ASSERT_NE(baked, nullptr);
        int lucideCodepoints = 0;
        for (unsigned int codepoint = ICON_MIN_LC; codepoint <= ICON_MAX_LC; ++codepoint) {
            if (!iconOnly->IsGlyphInFont(static_cast<ImWchar>(codepoint))) {
                continue;
            }
            ++lucideCodepoints;
            const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
            ASSERT_NE(glyph, nullptr) << "U+" << std::hex << codepoint;
            EXPECT_EQ(glyph->SourceIdx, kIconSourceIdx) << "U+" << std::hex << codepoint << " is not drawn from Lucide";
        }
        EXPECT_GT(lucideCodepoints, 1000);
    }

    TEST_F(ImGuiFontsTest, TheTextOfTheUiFontStaysWithInterAndTheMonoFontHasNoIcons) {
        const ImGuiFonts fonts = LoadImGuiFonts(ImGui::GetIO(), buffers);

        ASSERT_NE(fonts.ui, nullptr);
        ASSERT_NE(fonts.mono, nullptr);
        ImFontBaked* baked = fonts.ui->GetFontBaked(kUiFontSize);
        ASSERT_NE(baked, nullptr);
        for (const unsigned int codepoint : {0x0041u, 0x0061u, 0x0416u, 0x0436u}) {
            const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
            ASSERT_NE(glyph, nullptr) << "U+" << std::hex << codepoint;
            EXPECT_EQ(glyph->SourceIdx, kTextSourceIdx) << "U+" << std::hex << codepoint;
        }
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
