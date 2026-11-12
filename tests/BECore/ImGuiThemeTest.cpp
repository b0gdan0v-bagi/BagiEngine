#include <Common/Printers.h>
#include <Widgets/ImGuiTheme.h>
#include <cmath>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
        /// Smallest luminance step that still reads as a different surface on a dark theme
        constexpr float kMinSurfaceStep = 0.025f;
        constexpr float kMinTextContrast = 0.35f;

        float Luminance(const ImVec4& colour) {
            return 0.2126f * colour.x + 0.7152f * colour.y + 0.0722f * colour.z;
        }

        bool SameColour(const ImVec4& a, const ImVec4& b) {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        bool SameSizes(const ImGuiStyle& a, const ImGuiStyle& b) {
            return a.WindowPadding.x == b.WindowPadding.x && a.WindowPadding.y == b.WindowPadding.y && a.FramePadding.x == b.FramePadding.x && a.ItemSpacing.y == b.ItemSpacing.y &&
                   a.FrameRounding == b.FrameRounding && a.PopupRounding == b.PopupRounding && a.ScrollbarSize == b.ScrollbarSize && a.GrabMinSize == b.GrabMinSize &&
                   a.IndentSpacing == b.IndentSpacing;
        }

    }  // namespace

    TEST(ImGuiThemeTest, TheSurfacesDifferByAClearStepFromTheDeepestToTheNearest) {
        const ImGuiPalette& p = GetImGuiPalette();
        EXPECT_GE(Luminance(p.panelBg) - Luminance(p.windowBg), kMinSurfaceStep);
        EXPECT_GE(Luminance(p.frameBg) - Luminance(p.panelBg), kMinSurfaceStep);
    }

    TEST(ImGuiThemeTest, TheAccentAndTheTextAreNotTheSameColour) {
        const ImGuiPalette& p = GetImGuiPalette();
        EXPECT_FALSE(SameColour(p.accent, p.text));
        EXPECT_GE(std::fabs(Luminance(p.text) - Luminance(p.accent)), kMinTextContrast * 0.5f);
        EXPECT_GE(Luminance(p.text) - Luminance(p.panelBg), kMinTextContrast);
        EXPECT_LT(Luminance(p.textDisabled), Luminance(p.text));
    }

    TEST(ImGuiThemeTest, TheStatusRolesAreDistinct) {
        const ImGuiPalette& p = GetImGuiPalette();
        EXPECT_FALSE(SameColour(p.success, p.warning));
        EXPECT_FALSE(SameColour(p.warning, p.error));
        EXPECT_FALSE(SameColour(p.success, p.error));
    }

    TEST(ImGuiThemeTest, TheRolesAndTheSurfacesAreOpaque) {
        const ImGuiPalette& p = GetImGuiPalette();
        for (const ImVec4* role :
             {&p.windowBg, &p.panelBg, &p.frameBg, &p.border, &p.text, &p.textDisabled, &p.accent, &p.accentHover, &p.accentActive, &p.accentMuted, &p.success, &p.warning, &p.error}) {
            EXPECT_FLOAT_EQ(role->w, 1.0f);
        }

        ImGuiStyle style;
        ApplyImGuiTheme(style, 1.0f);
        for (const ImGuiCol colour : {ImGuiCol_Text, ImGuiCol_WindowBg, ImGuiCol_PopupBg, ImGuiCol_FrameBg, ImGuiCol_TitleBg, ImGuiCol_MenuBarBg, ImGuiCol_Button, ImGuiCol_Header, ImGuiCol_Tab,
                                      ImGuiCol_TabSelected, ImGuiCol_DockingEmptyBg, ImGuiCol_CheckMark, ImGuiCol_SliderGrab}) {
            EXPECT_FLOAT_EQ(style.Colors[colour].w, 1.0f) << "colour " << colour;
        }
    }

    TEST(ImGuiThemeTest, TheStyleTakesItsColoursFromThePalette) {
        const ImGuiPalette& p = GetImGuiPalette();
        ImGuiStyle style;
        ApplyImGuiTheme(style, 1.0f);

        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_WindowBg], p.panelBg));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_DockingEmptyBg], p.windowBg));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_CheckMark], p.accent));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_SliderGrab], p.accent));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_TabSelectedOverline], p.accent));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_Header], p.accentMuted));
        EXPECT_TRUE(SameColour(style.Colors[ImGuiCol_UnsavedMarker], p.warning));
        EXPECT_FLOAT_EQ(style.Colors[ImGuiCol_DockingPreview].x, p.accent.x);
    }

    TEST(ImGuiThemeTest, TheSizesAreLargerThanTheStockOnes) {
        const ImGuiStyle stock;
        ImGuiStyle style;
        ApplyImGuiTheme(style, 1.0f);

        EXPECT_GT(style.FramePadding.y, stock.FramePadding.y);
        EXPECT_GT(style.ItemSpacing.y, stock.ItemSpacing.y);
        EXPECT_GE(style.FrameRounding, 4.0f);
        EXPECT_LE(style.FrameRounding, 6.0f);
    }

    TEST(ImGuiThemeTest, ApplyingTwiceAtTheSameScaleGivesTheSameStyleAsOnce) {
        ImGuiStyle once;
        ApplyImGuiTheme(once, 2.0f);
        ImGuiStyle twice;
        ApplyImGuiTheme(twice, 2.0f);
        ApplyImGuiTheme(twice, 2.0f);

        EXPECT_TRUE(SameSizes(once, twice));
        EXPECT_GT(once.WindowPadding.x, 0.0f);

        ImGuiStyle unscaled;
        ApplyImGuiTheme(unscaled, 1.0f);
        EXPECT_FLOAT_EQ(once.WindowPadding.x, unscaled.WindowPadding.x * 2.0f);
        EXPECT_FLOAT_EQ(once.ScrollbarSize, unscaled.ScrollbarSize * 2.0f);
    }

    TEST(ImGuiThemeTest, AScaleChangeStartsFromTheBaseSizes) {
        ImGuiStyle changed;
        ApplyImGuiTheme(changed, 2.0f);
        ApplyImGuiTheme(changed, 1.0f);
        ImGuiStyle fresh;
        ApplyImGuiTheme(fresh, 1.0f);

        EXPECT_TRUE(SameSizes(changed, fresh));
    }

    TEST(ImGuiThemeTest, TheFontFieldsAreKept) {
        ImGuiStyle style;
        style.FontSizeBase = 14.0f;
        style.FontScaleMain = 1.25f;
        style.FontScaleDpi = 1.5f;

        ApplyImGuiTheme(style, 2.0f);

        EXPECT_FLOAT_EQ(style.FontSizeBase, 14.0f);
        EXPECT_FLOAT_EQ(style.FontScaleMain, 1.25f);
        EXPECT_FLOAT_EQ(style.FontScaleDpi, 1.5f);
    }

    TEST(ImGuiThemeTest, AScaleThatCannotBeOneCountsAsOne) {
        ImGuiStyle reference;
        ApplyImGuiTheme(reference, 1.0f);

        for (const float scale : {0.0f, -1.0f, kNaN, std::numeric_limits<float>::infinity()}) {
            ImGuiStyle style;
            ApplyImGuiTheme(style, scale);
            EXPECT_TRUE(SameSizes(style, reference)) << "scale " << scale;
        }
    }

}  // namespace BECore::Tests
