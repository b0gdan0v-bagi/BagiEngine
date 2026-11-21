// UIAmbient: the language and the window size as levels of a Context, so a Derived screen value rebuilds on a language switch or a canvas resize and not otherwise.

#include <BECore/UI/UIAmbient.h>
#include <BECore/Values/Derived/Derived.h>
#include <BECore/Values/Derived/DerivedReads.h>
#include <Common/UITestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr const char* kAmbientXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Hud" layer="10"><components>
      <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr float kWideWidth = 800.0f;
        constexpr float kWideHeight = 600.0f;
        constexpr float kNarrowWidth = 400.0f;

        /// A screen the way a game writes it: the declaration and the context once, a `Derived` text over each level
        struct AmbientScreen {
            ContextDecl declaration;
            UIAmbientLevels levels;
            Context context;
            Value<PoolString> language;
            Value<float> width;
            Derived<String> languageText;
            Derived<String> widthText;

            AmbientScreen(const UIStrings& strings, const UICanvasComponent& canvas) : languageText(DerivedReads{}), widthText(DerivedReads{}) {
                const auto declared = UIAmbient::Declare(declaration);
                EXPECT_TRUE(declared.has_value());
                levels = declared.value_or(UIAmbientLevels{});
                EXPECT_TRUE(language.Link(declaration, "language.language").has_value());
                EXPECT_TRUE(width.Link(declaration, "window.outputWidth").has_value());
                context = Context(declaration);
                UIAmbient::Fill(context, levels, strings, canvas.GetWindowView(), canvas.GetWindowVersion());
                DerivedReads languageReads;
                languageReads.Add(language);
                languageText.Reset(languageReads);
                DerivedReads widthReads;
                widthReads.Add(width);
                widthText.Reset(widthReads);
            }

            const String& Language() {
                return languageText.Get(context, [this](String& out, const Context& view) {
                    out.clear();
                    const StringView id = language.Get(view).ToStringView();
                    out.append(id.data(), id.size());
                });
            }

            const String& Width() {
                return widthText.Get(context, [this](String& out, const Context& view) {
                    out.clear();
                    out += Format("{}", width.Get(view));
                });
            }
        };

    }  // namespace

    class UIAmbientTest : public UISceneFixture {
    protected:
        void SetUp() override {
            UISceneFixture::SetUp();
            ASSERT_NO_FATAL_FAILURE(LoadScene(kAmbientXml));
            ASSERT_TRUE(_canvas);
            UIStrings& strings = CoreManager::GetUIStrings();
            strings.Set("en"_intern, "menu.start", "Start");
            strings.Set("ru"_intern, "menu.start", "Start (ru)");
            strings.SetLanguage("en"_intern);
            _canvas->UpdateLayout(kWideWidth, kWideHeight);
        }
    };

    TEST_F(UIAmbientTest, DeclareGivesTheTwoLevelsAndTheirPathsLink) {
        ContextDecl declaration;
        const auto levels = UIAmbient::Declare(declaration);
        ASSERT_TRUE(levels.has_value());
        EXPECT_EQ(declaration.IndexOf(UIAmbient::kLanguageLevel), levels->language);
        EXPECT_EQ(declaration.IndexOf(UIAmbient::kWindowLevel), levels->window);

        Value<PoolString> language;
        Value<PoolString> fallback;
        Value<float> width;
        Value<float> density;
        EXPECT_TRUE(language.Link(declaration, "language.language").has_value());
        EXPECT_TRUE(fallback.Link(declaration, "language.defaultLanguage").has_value());
        EXPECT_TRUE(width.Link(declaration, "window.outputWidth").has_value());
        EXPECT_TRUE(density.Link(declaration, "window.pixelDensity").has_value());
        EXPECT_EQ(language.Reads(), LevelBit(levels->language));
        EXPECT_EQ(width.Reads(), LevelBit(levels->window));

        EXPECT_FALSE(UIAmbient::Declare(declaration).has_value()) << "a second declaration repeats the names";
    }

    TEST_F(UIAmbientTest, TheViewsHoldWhatTheOwnersHold) {
        AmbientScreen screen(CoreManager::GetUIStrings(), *_canvas);

        EXPECT_EQ(screen.Language(), "en");
        EXPECT_EQ(screen.Width(), "800");
    }

    TEST_F(UIAmbientTest, ALanguageValueRebuildsOnASwitchAndOnAStringAndNotOnAVariable) {
        UIStrings& strings = CoreManager::GetUIStrings();
        AmbientScreen screen(strings, *_canvas);
        EXPECT_EQ(screen.Language(), "en");
        ASSERT_EQ(screen.languageText.Rebuilds(), 1U);

        static_cast<void>(screen.Language());
        EXPECT_EQ(screen.languageText.Rebuilds(), 1U);

        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(screen.Language(), "ru");
        EXPECT_EQ(screen.languageText.Rebuilds(), 2U);

        strings.Set("ru"_intern, "menu.start", "Go");
        static_cast<void>(screen.Language());
        EXPECT_EQ(screen.languageText.Rebuilds(), 3U) << "versions are per level: a string moves the language level too";

        CoreManager::GetUIVars().Set("score"_intern, 5);
        static_cast<void>(screen.Language());
        EXPECT_EQ(screen.languageText.Rebuilds(), 3U);
    }

    TEST_F(UIAmbientTest, ADefaultLanguageChangeIsSeenAndAnUnknownLanguageChangesNothing) {
        UIStrings& strings = CoreManager::GetUIStrings();
        AmbientScreen screen(strings, *_canvas);
        static_cast<void>(screen.Language());

        EXPECT_FALSE(strings.SetLanguage("de"_intern));
        static_cast<void>(screen.Language());
        EXPECT_EQ(screen.languageText.Rebuilds(), 1U);
        EXPECT_EQ(strings.GetLanguageView().language, "en"_intern);

        strings.SetDefaultLanguage("ru"_intern);
        EXPECT_EQ(strings.GetLanguageView().defaultLanguage, "ru"_intern);
        static_cast<void>(screen.Language());
        EXPECT_EQ(screen.languageText.Rebuilds(), 2U);

        strings.Clear();
        EXPECT_TRUE(strings.GetLanguageView().language.Empty());
        EXPECT_TRUE(strings.GetLanguageView().defaultLanguage.Empty());
    }

    TEST_F(UIAmbientTest, AWindowValueRebuildsWhenTheCanvasLaysOutForAnotherSizeAndNotForTheSame) {
        AmbientScreen screen(CoreManager::GetUIStrings(), *_canvas);
        EXPECT_EQ(screen.Width(), "800");
        ASSERT_EQ(screen.widthText.Rebuilds(), 1U);

        _canvas->UpdateLayout(kWideWidth, kWideHeight);
        _canvas->UpdateLayout(kWideWidth, kWideHeight);
        EXPECT_EQ(screen.Width(), "800");
        EXPECT_EQ(screen.widthText.Rebuilds(), 1U);

        _canvas->UpdateLayout(kNarrowWidth, kWideHeight);
        EXPECT_EQ(screen.Width(), "400");
        EXPECT_EQ(screen.widthText.Rebuilds(), 2U);

        CoreManager::GetUIVars().Set("score"_intern, 5);
        _canvas->UpdateLayout(kNarrowWidth, kWideHeight);
        static_cast<void>(screen.Width());
        EXPECT_EQ(screen.widthText.Rebuilds(), 2U);
    }

}  // namespace BECore::Tests
