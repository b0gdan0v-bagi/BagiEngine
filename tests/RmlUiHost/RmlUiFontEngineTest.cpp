#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUiHost/RmlUiFontEngine.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kFontPath = "assets/fonts/PT_Sans-Web-Regular.ttf";
        constexpr int kSmall = 16;
        constexpr int kBig = 32;
        constexpr float kLetterSpacing = 3.0f;

    }  // namespace

    /// The font engine on its own: no RmlUi context and no renderer, only the engine's font service
    class RmlUiFontEngineTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            ASSERT_TRUE(_engine.LoadFontFace(Rml::String(kFontPath), 0, false, Rml::Style::FontWeight::Auto));
        }

        void TearDown() override {
            _engine.Shutdown();
        }

        Rml::FontFaceHandle Handle(int size, const char* family = "PT Sans", Rml::Style::FontStyle style = Rml::Style::FontStyle::Normal,
                                   Rml::Style::FontWeight weight = Rml::Style::FontWeight::Auto) {
            return _engine.GetFontFaceHandle(family, style, weight, size);
        }

        int Width(Rml::FontFaceHandle handle, const char* text, float letterSpacing = 0.0f, Rml::Style::FontKerning kerning = Rml::Style::FontKerning::Auto) {
            const Rml::TextShapingContext context{_language, Rml::Style::Direction::Auto, kerning, letterSpacing};
            return _engine.GetStringWidth(handle, Rml::StringView(text, text + std::strlen(text)), context, Rml::Character::Null);
        }

        RmlUiFontEngine _engine;
        Rml::String _language;
    };

    TEST_F(RmlUiFontEngineTest, TheFontIsRegisteredUnderTheFamilyItSays) {
        EXPECT_EQ(_engine.GetFaceCount(), 1u);
        EXPECT_NE(Handle(kSmall), 0u);
        // A CSS family name is matched without regard to case
        EXPECT_NE(Handle(kSmall, "pt sans"), 0u);
    }

    TEST_F(RmlUiFontEngineTest, AnUnknownFamilyOrASizeOfZeroHasNoHandle) {
        EXPECT_EQ(Handle(kSmall, "No Such Family"), 0u);
        EXPECT_EQ(Handle(0), 0u);
        EXPECT_EQ(Handle(-4), 0u);
    }

    TEST_F(RmlUiFontEngineTest, ASizeOfAFaceHasOneHandle) {
        const Rml::FontFaceHandle small = Handle(kSmall);

        EXPECT_EQ(Handle(kSmall), small);
        EXPECT_NE(Handle(kBig), small);
    }

    TEST_F(RmlUiFontEngineTest, TheMetricsFollowTheSize) {
        const Rml::FontMetrics& small = _engine.GetFontMetrics(Handle(kSmall));
        const Rml::FontMetrics& big = _engine.GetFontMetrics(Handle(kBig));

        EXPECT_EQ(small.size, kSmall);
        EXPECT_GT(small.ascent, 0.0f);
        EXPECT_GT(small.descent, 0.0f);  // RmlUi counts it positive below the baseline
        EXPECT_GE(small.line_spacing, small.ascent + small.descent);
        EXPECT_GT(small.x_height, 0.0f);
        EXPECT_GT(small.underline_thickness, 0.0f);
        EXPECT_TRUE(small.has_ellipsis);
        EXPECT_GT(big.ascent, small.ascent);
        EXPECT_GT(big.line_spacing, small.line_spacing);
    }

    TEST_F(RmlUiFontEngineTest, TheMetricsOfNoHandleAreEmpty) {
        const Rml::FontMetrics& none = _engine.GetFontMetrics(0);

        EXPECT_EQ(none.size, 0);
        EXPECT_EQ(Width(0, "text"), 0);
    }

    TEST_F(RmlUiFontEngineTest, TheWidthGrowsWithTheTextAndTheSize) {
        const Rml::FontFaceHandle small = Handle(kSmall);
        const Rml::FontFaceHandle big = Handle(kBig);

        EXPECT_EQ(Width(small, ""), 0);
        EXPECT_GT(Width(small, "i"), 0);
        EXPECT_GT(Width(small, "iii"), Width(small, "i"));
        EXPECT_GT(Width(big, "Hello"), Width(small, "Hello"));
        // The widths of two letters add up (PT Sans keeps its kerning where the engine does not read it)
        EXPECT_NEAR(Width(small, "ab", 0.0f, Rml::Style::FontKerning::None), Width(small, "a") + Width(small, "b"), 1.0);
    }

    TEST_F(RmlUiFontEngineTest, CyrillicAndASpaceHaveAWidth) {
        const Rml::FontFaceHandle handle = Handle(kBig);

        EXPECT_GT(Width(handle, "Привет"), 0);
        EXPECT_GT(Width(handle, " "), 0);
    }

    TEST_F(RmlUiFontEngineTest, AControlCharacterHasNoWidth) {
        EXPECT_EQ(Width(Handle(kSmall), "\t\n\x01"), 0);
    }

    TEST_F(RmlUiFontEngineTest, LetterSpacingAddsToEveryLetter) {
        const Rml::FontFaceHandle handle = Handle(kBig);
        const int plain = Width(handle, "Hello", 0.0f, Rml::Style::FontKerning::None);
        const int spaced = Width(handle, "Hello", kLetterSpacing, Rml::Style::FontKerning::None);

        EXPECT_NEAR(spaced - plain, 5 * kLetterSpacing, 1.0);
    }

    TEST_F(RmlUiFontEngineTest, TheWeightPicksTheClosestFaceOfAFamily) {
        ASSERT_TRUE(_engine.LoadFontFace(Rml::String(kFontPath), 0, "Test", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal, false));
        ASSERT_TRUE(_engine.LoadFontFace(Rml::String(kFontPath), 0, "Test", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold, false));

        const Rml::FontFaceHandle normal = Handle(kSmall, "Test", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
        const Rml::FontFaceHandle bold = Handle(kSmall, "Test", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold);

        EXPECT_NE(normal, 0u);
        EXPECT_NE(bold, 0u);
        EXPECT_NE(normal, bold);
        // No italic face exists: the closest in weight of the other style is taken
        EXPECT_EQ(Handle(kSmall, "Test", Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal), normal);
    }

    TEST_F(RmlUiFontEngineTest, OnlyTheFirstFaceOfAFileIsRead) {
        EXPECT_FALSE(_engine.LoadFontFace(Rml::String(kFontPath), 1, false, Rml::Style::FontWeight::Auto));
        EXPECT_FALSE(_engine.LoadFontFace(Rml::String("assets/fonts/missing.ttf"), 0, false, Rml::Style::FontWeight::Auto));
        EXPECT_EQ(_engine.GetFaceCount(), 1u);
    }

    TEST_F(RmlUiFontEngineTest, ReleasingTheResourcesKeepsTheFacesAndInvalidatesTheHandles) {
        ASSERT_NE(Handle(kSmall), 0u);

        _engine.ReleaseFontResources();

        EXPECT_EQ(_engine.GetFaceCount(), 1u);
        EXPECT_EQ(_engine.GetPageCount(), 0u);
        EXPECT_NE(Handle(kSmall), 0u);
    }

    TEST_F(RmlUiFontEngineTest, FontEffectsAreRefusedWithoutABreak) {
        Rml::FontEffectList effects;
        effects.emplace_back();

        EXPECT_EQ(_engine.PrepareFontEffects(Handle(kSmall), effects), 0u);
        EXPECT_EQ(_engine.PrepareFontEffects(Handle(kSmall), Rml::FontEffectList()), 0u);
    }

}  // namespace BECore::Tests
