#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIToggle.h>
#include <BECore/UI/UIVars.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <Common/UIThemeTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The screen test of a screen written with theme tokens, the template for a game's own (.claude/rules/ui-layout.md, "A screen test"; the language twin is UISettingsScreenTest).
     * The screen is a card (a stack on a `@panel` fill) with a title, a strip, a composed button (a `UIElement` with a `UITextComponent` and an `UIInputClicker`), a `UIToggle` and a
     * part written in HTML and CSS with `var(--note)`, compiled by `UIWebCompiler` into the same card. Three themes: `light` (the default), `dark`, and `soft`, which leaves the token
     * `track` to the default theme.
     *
     *  - **One run per theme** (`ForEachTheme`): the size matrix, the fit of every word, the legibility check and "no token is missing" run once for every theme of the table, so a
     *    theme added to the table is covered without touching the test. The table is built here; the engine ships none, and a game builds its own from `config/UIThemes.xml`.
     *  - **Legibility** is the one thing the validator cannot see: the text colour against the colour it is drawn on, as the WCAG contrast ratio, at least `kMinContrast`.
     *  - **The checks are held to account:** a broken theme (a text colour that is the plate's, an unknown token) must be found by them.
     */

    namespace {

        constexpr float kViewWidth = 640.0f;
        constexpr float kViewHeight = 480.0f;
        constexpr float kRowHeight = 56.0f;
        constexpr float kMinContrast = 4.5f;  // WCAG AA for normal text

        constexpr const char* kLight = "light";
        constexpr const char* kDark = "dark";
        constexpr const char* kSoft = "soft";

        // The card. @NOTE@ is replaced by the elements the HTML and CSS front-end compiled
        constexpr const char* kThemeSceneXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main" viewWidth="640" viewHeight="480"><nodes>
    <node name="Screen" layer="10"><components>
      <component type="UICanvasComponent" space="SceneView">
        <elements>
          <element type="UIStack" name="Card" anchor="Center" width="94%" maxWidth="560" height="304" direction="Vertical" spacing="12" padding="16" crossAlign="Stretch" color="@panel">
            <children>
              <element type="UILabel" name="Title" height="44" text="Themes" fontSize="30" align="Center" textColor="@text"/>
              <element type="UIPanel" name="Strip" height="12" color="@accent"/>
              <element type="UIElement" name="Action" height="56" color="@button" hoverColor="@buttonHover">
                <components>
                  <component type="UITextComponent" text="Go" fontSize="26" align="Center" verticalAlign="Center" textColor="@buttonText"/>
                  <component type="UIInputClicker" action="ThemeGo"/>
                </components>
              </element>
              <element type="UIToggle" name="Sound" bind="sound" action="ThemeSound" height="56" text="Sound" fontSize="24" verticalAlign="Center" textColor="@text" trackColor="@track" fillColor="@accent" thumbColor="@text"/>
              @NOTE@
            </children>
          </element>
        </elements>
      </component>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        constexpr const char* kNoteHtml = R"(<div id="Note">Compiled</div>)";
        constexpr const char* kNoteCss = R"(
#Note { height: 56px; background: var(--note); color: var(--text); font-size: 24px; text-align: center; vertical-align: middle; }
)";

        /// The tokens of the screen, per theme. `soft` has no `track`
        void FillThemes(UIThemes& themes) {
            const PoolString light = PoolString::Intern(kLight);
            const PoolString dark = PoolString::Intern(kDark);
            const PoolString soft = PoolString::Intern(kSoft);
            themes.Set(light, "panel", Color{240, 240, 230, 255});
            themes.Set(light, "text", Color{20, 24, 32, 255});
            themes.Set(light, "accent", Color{40, 120, 80, 255});
            themes.Set(light, "button", Color{40, 90, 60, 255});
            themes.Set(light, "buttonHover", Color{60, 120, 85, 255});
            themes.Set(light, "buttonText", Color{255, 255, 255, 255});
            themes.Set(light, "track", Color{150, 150, 150, 255});
            themes.Set(light, "note", Color{220, 230, 245, 255});
            themes.SetDefaultTheme(light);

            themes.Set(dark, "panel", Color{30, 32, 40, 255});
            themes.Set(dark, "text", Color{235, 235, 230, 255});
            themes.Set(dark, "accent", Color{200, 180, 40, 255});
            themes.Set(dark, "button", Color{60, 70, 110, 255});
            themes.Set(dark, "buttonHover", Color{80, 92, 140, 255});
            themes.Set(dark, "buttonText", Color{255, 255, 255, 255});
            themes.Set(dark, "track", Color{90, 90, 100, 255});
            themes.Set(dark, "note", Color{50, 56, 70, 255});

            themes.Set(soft, "panel", Color{250, 244, 230, 255});
            themes.Set(soft, "text", Color{60, 45, 30, 255});
            themes.Set(soft, "accent", Color{200, 120, 60, 255});
            themes.Set(soft, "button", Color{150, 70, 40, 255});
            themes.Set(soft, "buttonHover", Color{175, 90, 55, 255});
            themes.Set(soft, "buttonText", Color{255, 255, 255, 255});
            themes.Set(soft, "note", Color{235, 225, 205, 255});
        }

        /// The WCAG relative luminance of an opaque colour
        float Luminance(const Color& color) {
            const auto linear = [](uint8_t channel) {
                const float value = static_cast<float>(channel) / 255.0f;
                return value <= 0.03928f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
            };
            return 0.2126f * linear(color.r) + 0.7152f * linear(color.g) + 0.0722f * linear(color.b);
        }

        /// The WCAG contrast ratio of two colours, 1 (the same) to 21 (black on white)
        float ContrastRatio(const Color& a, const Color& b) {
            const float first = Luminance(a);
            const float second = Luminance(b);
            return (std::max(first, second) + 0.05f) / (std::min(first, second) + 0.05f);
        }

        std::string Spliced(const std::string& note) {
            std::string xml = kThemeSceneXml;
            const std::string marker = "@NOTE@";
            xml.replace(xml.find(marker), marker.size(), note);
            return xml;
        }

        /// The elements of a compiled `<ui><elements>...</elements></ui>`, as text to put into another file
        std::string CompiledElements(const eastl::string& xml) {
            const std::string text(xml.c_str(), xml.size());
            const size_t begin = text.find("<elements>");
            const size_t end = text.rfind("</elements>");
            if (begin == std::string::npos || end == std::string::npos) {
                return {};
            }
            return text.substr(begin + std::string("<elements>").size(), end - begin - std::string("<elements>").size());
        }

    }  // namespace

    class UIThemeScreenTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            FillThemes(CoreManager::GetUIThemes());
            CoreManager::GetUIVars().Set("sound"_intern, "true");
            _renderer = New<RecordingRenderer>();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);
        }

        void TearDown() override {
            _canvas.Reset();
            _manager.Reset();
            CoreManager::GetUIThemes().Clear();  // the table is global: the next test starts with none
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        /// The card, with the HTML and CSS part compiled into it; @p css is the style sheet of the HTML part (the broken copies change it)
        void LoadScene(const char* css = kNoteCss) {
            _canvas.Reset();
            _manager.Reset();
            UIWebReport report;
            const std::string source = std::string("<style>") + css + "</style>" + kNoteHtml;
            const eastl::string compiled = UIWebCompiler::Compile(eastl::string_view(source.data(), source.size()), {}, report);
            ASSERT_FALSE(report.HasIssues()) << report.issues[0].message.c_str() << " / " << compiled.c_str();
            const std::string note = CompiledElements(compiled);
            ASSERT_FALSE(note.empty()) << compiled.c_str();

            XmlDeserializer deserializer;
            const std::string xml = Spliced(note);
            ASSERT_TRUE(deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            _canvas = _manager->GetSceneByName("Main"_intern)->GetRootNode().GetChildren().front()->GetComponent<UICanvasComponent>();
            ASSERT_TRUE(_canvas);
            _canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        }

        Scene& GetScene() const {
            return *_manager->GetSceneByName("Main"_intern);
        }

        /// A draw resolves the tokens of every element that is drawn, in the theme that is current
        void Draw() {
            _manager->SetActiveScene("Main"_intern);
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        UIElement& Find(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            if (element == nullptr) {
                ADD_FAILURE() << name << " is not in the screen";
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *element;
        }

        template <typename T>
        T& Get(const char* name) const {
            UIElement* element = _canvas->GetRoot().FindByName(PoolString::Intern(name));
            T* typed = element ? element->Cast<T>() : nullptr;
            if (typed == nullptr) {
                ADD_FAILURE() << name << (element ? " is not the expected control" : " is not in the screen");
                std::abort();  // a reference to nothing cannot be returned; the failure above is printed first
            }
            return *typed;
        }

        UILayoutContext ViewContext() const {
            UILayoutContext context;
            context.sceneViewWidth = GetScene().GetViewWidth();
            context.sceneViewHeight = GetScene().GetViewHeight();
            return context;
        }

        /// The card on the whole matrix, in the theme that is current: the validator does not read colours, but a theme must not change what it finds
        eastl::vector<UIMatrixFailure> ValidateCard() const {
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            return ValidateOnMatrix(_canvas->GetLayout(), options, ViewContext());
        }

        /// Text that its control cuts, one problem to a line (an ellipsis label can shrink, so the validator does not report it)
        eastl::string ContentProblems() const {
            eastl::string problems;
            UICanvasLayout& layout = _canvas->GetLayout();
            UILayoutContext context = ViewContext();
            context.outputWidth = kViewWidth;
            context.outputHeight = kViewHeight;
            layout.Update(context);
            const float scale = layout.GetResolved().scale;
            for (const char* name : {"Title", "Action", "Sound", "Note"}) {
                const UIElement& element = Find(name);
                const UIContentFit fit = element.CheckContent(element.rect.w, element.rect.h, scale);
                if (fit.hasContent && !fit.fits) {
                    problems += Format("{} cuts its text in {:.1f} units\n", name, element.rect.w);
                }
            }
            layout.Invalidate();
            return problems;
        }

        /// Each text colour against the colour it is drawn on, in the theme that is current and after a draw: one line for every pair under kMinContrast. The channels are the
        /// resolved ones, so a token that fell to magenta is judged as magenta
        eastl::string LegibilityProblems() {
            Draw();
            const Color panel = Find("Card").color;
            const UITextComponent* goText = Find("Action").GetComponent<UITextComponent>();
            const struct {
                const char* what;
                Color text;
                Color plate;
            } pairs[] = {{"Title on the card", Get<UILabel>("Title").textColor, panel},
                         {"Sound on the card", Get<UIToggle>("Sound").textColor, panel},
                         {"Go on its button", goText ? Color(goText->textColor) : Color{}, Find("Action").color},
                         {"Note on its plate", Get<UILabel>("Note").textColor, Find("Note").color}};
            eastl::string problems;
            for (const auto& pair : pairs) {
                const float ratio = ContrastRatio(pair.text, pair.plate);
                if (ratio < kMinContrast) {
                    problems += Format("{}: contrast {:.2f}, under {:.1f}\n", pair.what, ratio, kMinContrast);
                }
            }
            return problems;
        }

        /// The tokens no theme defined that the last draws met
        eastl::string MissingTokens() const {
            eastl::string text;
            for (const PoolString token : CoreManager::GetUIThemes().GetMissing()) {
                text += Format("the token '{}' is in no theme\n", token.ToStringView());
            }
            return text;
        }

        /// The colours the last draw filled rects with, in draw order: what a theme looks like to the renderer
        eastl::vector<Color> Fills() const {
            eastl::vector<Color> fills;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect) {
                    fills.push_back(call.color);
                }
            }
            return fills;
        }

        static bool SameFills(const eastl::vector<Color>& a, const eastl::vector<Color>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (!ColorsEqual(a[i], b[i])) {
                    return false;
                }
            }
            return true;
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtr<UICanvasComponent> _canvas;
    };

    // ---- once per theme ------------------------------------------------------------------------------------------------------

    TEST_F(UIThemeScreenTest, TheScreenPassesTheMatrixTheFitAndTheLegibilityChecksInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UIThemes& themes = CoreManager::GetUIThemes();
        size_t visited = 0;
        ForEachTheme(themes, [&](PoolString theme) {
            ++visited;
            const char* name = theme.ToStringView().data();
            const auto failures = ValidateCard();
            EXPECT_TRUE(failures.empty()) << name << ": " << DescribeMatrixFailures(failures).c_str();
            const eastl::string content = ContentProblems();
            EXPECT_TRUE(content.empty()) << name << ": " << content.c_str();
            const eastl::string legibility = LegibilityProblems();
            EXPECT_TRUE(legibility.empty()) << name << ": " << legibility.c_str();
            EXPECT_TRUE(themes.GetMissing().empty()) << name << ": " << MissingTokens().c_str();
        });
        EXPECT_EQ(visited, 3u) << "light, dark and soft";
    }

    TEST_F(UIThemeScreenTest, ForEachThemeVisitsEveryThemeInTheOrderOfTheTableAndPutsTheCurrentOneBack) {
        UIThemes& themes = CoreManager::GetUIThemes();
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern(kDark)));
        eastl::vector<PoolString> visited;
        ForEachTheme(themes, [&](PoolString theme) {
            EXPECT_EQ(themes.GetTheme(), theme);
            visited.push_back(theme);
        });
        ASSERT_EQ(visited.size(), 3u);
        EXPECT_EQ(visited[0], PoolString::Intern(kLight));
        EXPECT_EQ(visited[1], PoolString::Intern(kDark));
        EXPECT_EQ(visited[2], PoolString::Intern(kSoft));
        EXPECT_EQ(themes.GetTheme(), PoolString::Intern(kDark));
    }

    TEST_F(UIThemeScreenTest, EveryThemeDrawsItsOwnFillsAndTheCompiledPartFollowsTheTheme) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UIThemes& themes = CoreManager::GetUIThemes();
        eastl::vector<eastl::vector<Color>> fills;
        ForEachTheme(themes, [&](PoolString) {
            Draw();
            fills.push_back(Fills());
        });
        ASSERT_EQ(fills.size(), 3u);
        for (size_t first = 0; first < fills.size(); ++first) {
            EXPECT_FALSE(fills[first].empty());
            for (size_t second = first + 1; second < fills.size(); ++second) {
                EXPECT_FALSE(SameFills(fills[first], fills[second])) << "themes " << first << " and " << second << " draw the same fills";
            }
        }

        // The part compiled from CSS is the one that moved with `--note`, like the parts written in XML
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern(kLight)));
        Draw();
        EXPECT_TRUE(ColorsEqual(Find("Note").color, Color{220, 230, 245, 255}));
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern(kDark)));
        Draw();
        EXPECT_TRUE(ColorsEqual(Find("Note").color, Color{50, 56, 70, 255}));
        EXPECT_EQ(Find("Note").color.token.ToStringView(), "note") << "the token stays";
    }

    TEST_F(UIThemeScreenTest, ATokenThatAThemeLeavesToTheDefaultThemeShowsTheDefaultsColour) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UIThemes& themes = CoreManager::GetUIThemes();
        ASSERT_TRUE(themes.SetTheme(PoolString::Intern(kSoft)));
        EXPECT_FALSE(themes.HasToken(PoolString::Intern(kSoft), "track"));
        Draw();
        EXPECT_TRUE(ColorsEqual(Get<UIToggle>("Sound").trackColor, Color{150, 150, 150, 255})) << "soft has no track: the light one is drawn";
        EXPECT_TRUE(themes.GetMissing().empty()) << "a fallback is not a missing token";
    }

    // ---- the table of themes ---------------------------------------------------------------------------------------------------

    TEST_F(UIThemeScreenTest, ExpectThemesCompleteNamesTheThemeAndTheTokenThatAreMissing) {
        UIThemes& themes = CoreManager::GetUIThemes();
        const eastl::string problems = MissingThemeTokens(themes);
        EXPECT_NE(problems.find("theme 'soft' does not define the token 'track'"), eastl::string::npos) << problems.c_str();
        EXPECT_EQ(problems.find("theme 'dark'"), eastl::string::npos);
        EXPECT_EQ(problems.find("theme 'light'"), eastl::string::npos);

        themes.Set(PoolString::Intern(kSoft), "track", Color{170, 160, 150, 255});
        EXPECT_TRUE(MissingThemeTokens(themes).empty());
        ExpectThemesComplete(themes);
    }

    // ---- a deliberately broken theme must be caught -----------------------------------------------------------------------------

    TEST_F(UIThemeScreenTest, ATextColourThatIsThePlatesColourIsCaughtByTheLegibilityCheck) {
        ASSERT_NO_FATAL_FAILURE(LoadScene());
        UIThemes& themes = CoreManager::GetUIThemes();
        const PoolString washed = PoolString::Intern("washed");
        for (const char* token : {"panel", "accent", "button", "buttonHover", "buttonText", "track", "note"}) {
            themes.Set(washed, token, *themes.Find(token));
        }
        themes.Set(washed, "text", Color{235, 235, 225, 255});  // the card is 240,240,230
        ASSERT_TRUE(themes.SetTheme(washed));

        const eastl::string problems = LegibilityProblems();
        EXPECT_NE(problems.find("Title on the card"), eastl::string::npos) << problems.c_str();
        EXPECT_NE(problems.find("Note on its plate"), eastl::string::npos) << problems.c_str();
        EXPECT_EQ(problems.find("Go on its button"), eastl::string::npos) << "the button's own text colour is fine";
        EXPECT_TRUE(ContentProblems().empty()) << "the checks are independent: the words fit";
    }

    TEST_F(UIThemeScreenTest, AnUnknownTokenDrawsMagentaAndIsFoundByTheMissingTokenCheck) {
        ASSERT_NO_FATAL_FAILURE(LoadScene(R"(#Note { height: 56px; background: var(--notes); color: var(--text); font-size: 24px; text-align: center; vertical-align: middle; })"));
        UIThemes& themes = CoreManager::GetUIThemes();
        ForEachTheme(themes, [&](PoolString) { Draw(); });
        EXPECT_TRUE(ColorsEqual(Find("Note").color, kMissingTokenColor));
        const eastl::string missing = MissingTokens();
        EXPECT_NE(missing.find("the token 'notes' is in no theme"), eastl::string::npos) << missing.c_str();
        EXPECT_EQ(themes.GetMissing().size(), 1u) << "once, though three themes drew it";
    }

}  // namespace BECore::Tests
