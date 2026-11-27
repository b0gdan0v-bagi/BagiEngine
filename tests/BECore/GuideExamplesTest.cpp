#include <BECore/Config/XmlConfig.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneLoader.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/TextComponent.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UIVars.h>
#include <BECore/UI/UIWebCompiler.h>
#include <Common/ContentFilesCheck.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Common/UIScreenTestSupport.h>
#include <Common/UIThemeTestSupport.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    /*
     * The example folders of the guide for people (`docs/guide/examples/<page>/`, `tools/docs/CLAUDE.md`) are content a game could ship, and a page excerpts them. These cases run
     * each folder through the engine, so a change of the UI that breaks an example fails `ctest` and not a reader. The folders are read from the source tree: a game's content is
     * not mounted in the test executable.
     */

    namespace {

        constexpr const char* kScreenSource = "assets/ui/settings.ui.html";
        constexpr size_t kReportedCases = 4;
        // The matrix labels a case "... <scale mode> x<user scale>". The plate has a fixed height, so it does not fit a small window at the user scale 1.5 (the accessibility
        // setting): the example is checked at 1.0, which every scale mode and every window of the matrix passes
        constexpr StringView kLargeUserScale = " x1.5";

        Path EngineRoot() {
            return Path(__FILE__).Parent().Parent().Parent();
        }

        Path UiScreenFolder() {
            return EngineRoot() / "docs" / "guide" / "examples" / "ui-screen";
        }

        Path PrefabFolder() {
            return EngineRoot() / "docs" / "guide" / "examples" / "prefab";
        }

        String ReadUiScreen(const char* relativePath) {
            return ReadFile(UiScreenFolder() / relativePath);
        }

        /// The `UIWebReport` of the example screen compiled with the sheet @p css in place of its own
        UIWebReport CompileScreen(const String& css, String& xml) {
            UIWebReport report;
            xml = UIWebCompiler::Compile(
                ReadUiScreen(kScreenSource),
                [&](StringView href) -> Optional<String> {
                    if (href != "settings.css") {
                        return std::nullopt;
                    }
                    return css;
                },
                report);
            return report;
        }

        String DescribeIssues(const UIWebReport& report) {
            String text;
            for (const UIWebIssue& issue : report.issues) {
                text += Format("{} line {}: {}\n", issue.source.c_str(), issue.line, issue.message.c_str());
            }
            return text;
        }

        /// The tokens a sheet reads through `var(--name)`
        Vector<String> TokensOf(StringView css) {
            constexpr StringView kOpen = "var(--";
            Vector<String> tokens;
            for (size_t at = css.find(kOpen); at != StringView::npos; at = css.find(kOpen, at)) {
                at += kOpen.size();
                const size_t end = css.find(')', at);
                if (end == StringView::npos) {
                    break;
                }
                tokens.emplace_back(css.substr(at, end - at));
            }
            return tokens;
        }

        void LoadXmlTable(StringView xml, auto&& load) {
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromString(xml));
            const Vector<String> problems = load(config.GetRoot());
            EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
        }

    }  // namespace

    class GuideExamplesTest : public UISceneFixture {
    protected:
        /// The example's scene with the screen read from @p screenFile (an absolute path: the folder is not mounted), the strings and the themes of the folder, and the variables at
        /// their longest values. The canvas of the node `Settings` is the one the lookups search
        void LoadUiScreen(StringView screenFile) {
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetUIThemes().Clear();
            ASSERT_NO_FATAL_FAILURE(LoadXmlTable(ReadUiScreen("config/UIStrings.xml"), [](const XmlNode& root) { return CoreManager::GetUIStrings().LoadFromXml(root); }));
            ASSERT_NO_FATAL_FAILURE(LoadXmlTable(ReadUiScreen("config/UIThemes.xml"), [](const XmlNode& root) { return CoreManager::GetUIThemes().LoadFromXml(root); }));
            CoreManager::GetUIVars().Set("soundOn"_intern, "true");
            CoreManager::GetUIVars().Set("volume"_intern, "0.5");
            CoreManager::GetUIVars().Set("language"_intern, "en");

            String scene = ReadUiScreen("config/SceneConfig.xml");
            ASSERT_NO_FATAL_FAILURE(Replace(scene, kScreenSource, screenFile));
            ASSERT_NO_FATAL_FAILURE(LoadScene(scene));
            ASSERT_NO_FATAL_FAILURE(UseCanvas("Settings"_intern, "DemoScene"_intern));
            Draw("DemoScene"_intern);  // the parameters of a canvas reach its layout on the first draw
        }

        void LoadUiScreen() {
            LoadUiScreen((UiScreenFolder() / kScreenSource).View());
        }

        /// The prefab example's scene text @p sceneText with its `<node ref>` expanded from the folder's `config/nodes/` (a game's file system is not mounted: the directory loader)
        void LoadPrefabScene(const String& sceneText) {
            ASSERT_TRUE(_sceneDocument.load_string(sceneText.c_str()));
            _manager = LoadSceneManager(XmlNode{_sceneDocument.document_element()}, MakeDirectoryNodeLoader(PrefabFolder() / "config"));
            ASSERT_NE(_manager, nullptr);
            _manager->Initialize();
        }

        /// The node @p name of the scene `DemoScene`, null when it is not there
        SceneNode* PrefabNode(PoolString name) const {
            Scene* scene = _manager ? _manager->GetSceneByName("DemoScene"_intern) : nullptr;
            if (scene == nullptr) {
                return nullptr;
            }
            for (const auto& child : scene->GetRootNode().GetChildren()) {
                if (child->GetName() == name) {
                    return child.Get();
                }
            }
            return nullptr;
        }

        /// What is wrong with the canvas on every case of the size matrix, in every language of the strings: the first lines of the first cases that have anything. The validator's
        /// issues come first, then `Problems()` (a word its control cuts, a control that takes a press and is under 56 units high)
        String ProblemsOnMatrix() {
            UICanvasLayout& layout = _canvas->GetLayout();
            UILayoutValidatorOptions options;
            options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
            String report;
            size_t reported = 0;
            ForEachLanguage([&](PoolString language) {
                for (const UIMatrixFailure& failure : ValidateOnMatrix(layout, options)) {
                    if (StringView(failure.label).ends_with(kLargeUserScale)) {
                        continue;
                    }
                    if (reported++ < kReportedCases) {
                        report +=
                            Format("[{}] {}: {} issues, the first on '{}'\n", language.ToStringView(), failure.label.c_str(), failure.issues.size(), failure.issues.front().element.ToStringView());
                    }
                }
                for (const UIMatrixCase& matrixCase : UILayoutMatrix()) {
                    UILayoutContext context;
                    context.outputWidth = matrixCase.context.outputWidth;
                    context.outputHeight = matrixCase.context.outputHeight;
                    context.safeArea = matrixCase.context.safeArea;
                    layout.Update(context);
                    const String problems = Problems(layout);
                    if (!problems.empty() && reported++ < kReportedCases) {
                        report += Format("[{}] {}:\n{}", language.ToStringView(), matrixCase.label.c_str(), problems.c_str());
                    }
                }
            });
            layout.Invalidate();
            return report;
        }

        pugi::xml_document _sceneDocument;
    };

    TEST_F(GuideExamplesTest, TheUiScreenCompilesWithAnEmptyReport) {
        String xml;
        const UIWebReport report = CompileScreen(ReadUiScreen("assets/ui/settings.css"), xml);
        EXPECT_TRUE(report.issues.empty()) << DescribeIssues(report).c_str();
        EXPECT_FALSE(xml.empty());
    }

    TEST_F(GuideExamplesTest, TheUiScreenSceneLoadsAndBuildsTheScreenWithoutAWarning) {
        LogRecorder log;
        ASSERT_NO_FATAL_FAILURE(LoadUiScreen());
        LogEvent::Flush();

        const Vector<String> warnings = log.Messages(LogLevel::Warning, {});
        EXPECT_TRUE(warnings.empty()) << (warnings.empty() ? "" : warnings[0].c_str());
        EXPECT_EQ(log.Count(LogLevel::Error, {}), 0u);
        for (const char* name : {"Plate", "Sound", "Volume", "LanguageEn", "LanguageRu", "Ok"}) {
            EXPECT_NE(FindElement(name), nullptr) << name << " is not in the screen";
        }
    }

    TEST_F(GuideExamplesTest, TheUiScreenPassesTheSizeMatrixInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(LoadUiScreen());
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguages().size(), 2u) << "en and ru";
        const String problems = ProblemsOnMatrix();
        EXPECT_TRUE(problems.empty()) << problems.c_str();
    }

    TEST_F(GuideExamplesTest, TheUiThemesLoadCleanAndEveryTokenOfTheSheetIsInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(LoadUiScreen());
        const UIThemes& themes = CoreManager::GetUIThemes();
        EXPECT_EQ(themes.GetThemes().size(), 2u) << "night and day";
        EXPECT_EQ(themes.GetDefaultTheme(), "night"_intern);
        ExpectThemesComplete(themes);

        const Vector<String> tokens = TokensOf(ReadUiScreen("assets/ui/settings.css"));
        EXPECT_FALSE(tokens.empty());
        for (const PoolString theme : themes.GetThemes()) {
            for (const String& token : tokens) {
                EXPECT_TRUE(themes.HasToken(theme, token)) << "the sheet reads '" << token.c_str() << "', theme '" << theme.ToStringView().data() << "' lacks it";
            }
        }
    }

    TEST_F(GuideExamplesTest, TheUiScreenIsDrawnInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(LoadUiScreen());
        LogRecorder log;
        ForEachTheme(CoreManager::GetUIThemes(), [&](PoolString) { Draw("DemoScene"_intern); });
        LogEvent::Flush();
        EXPECT_TRUE(CoreManager::GetUIThemes().GetMissing().empty()) << "a token of the sheet is in no theme";
        EXPECT_EQ(log.Count(LogLevel::Warning, {}), 0u);
    }

    TEST_F(GuideExamplesTest, TheContentFilesOfTheExampleFoldersAreAtTheCurrentVersion) {
        ExpectContentFilesAtCurrentVersion(EngineRoot() / "docs" / "guide" / "examples");
    }

    TEST_F(GuideExamplesTest, ThePrefabExampleGivesThreeInstancesWithTheirOwnOverrides) {
        LogRecorder log;
        ASSERT_NO_FATAL_FAILURE(LoadPrefabScene(ReadFile(PrefabFolder() / "config" / "SceneConfig.xml")));
        LogEvent::Flush();
        EXPECT_EQ(log.Count(LogLevel::Warning, {}), 0u);
        EXPECT_EQ(log.Count(LogLevel::Error, {}), 0u);

        SceneNode* sky = PrefabNode("Sky"_intern);
        SceneNode* one = PrefabNode("BadgeOne"_intern);
        SceneNode* two = PrefabNode("BadgeTwo"_intern);
        SceneNode* three = PrefabNode("BadgeThree"_intern);
        ASSERT_TRUE(sky != nullptr && one != nullptr && two != nullptr && three != nullptr);
        EXPECT_TRUE(sky->GetSourceRef().Empty());
        for (const SceneNode* badge : {one, two, three}) {
            EXPECT_EQ(badge->GetSourceRef(), "nodes/Badge"_intern) << badge->GetName().CStr();
            EXPECT_EQ(badge->GetComponents().size(), 3u) << "the prefab's Transform, Quad and Text, an override adds none";
        }

        // what an instance names changes; what it leaves out is the prefab's
        const auto oneTransform = one->GetComponent<TransformComponent>();
        const auto oneText = one->GetComponent<TextComponent>();
        ASSERT_TRUE(oneTransform && oneText);
        EXPECT_FLOAT_EQ(oneTransform->_x, 100.0f);
        EXPECT_FLOAT_EQ(oneTransform->_width, 220.0f);
        EXPECT_EQ(oneText->_text, "One");
        EXPECT_FLOAT_EQ(oneText->_fontSize, 32.0f);

        const auto twoText = two->GetComponent<TextComponent>();
        ASSERT_TRUE(twoText);
        EXPECT_EQ(twoText->_text, "Two");

        const Color prefabColor = one->GetComponent<QuadRendererComponent>()->_color;
        EXPECT_PRED2(ColorsEqual, two->GetComponent<QuadRendererComponent>()->_color, prefabColor);
        const Color threeColor = three->GetComponent<QuadRendererComponent>()->_color;
        EXPECT_PRED2(ColorsEqual, threeColor, Color(200, 110, 40, 255));
        EXPECT_FALSE(ColorsEqual(threeColor, prefabColor));
        EXPECT_EQ(three->GetComponent<TextComponent>()->_text, "Three");
    }

    // ---- broken copies: the cases above see a mistake -------------------------------------------------------------------

    TEST_F(GuideExamplesTest, ABrokenSheetWithJustifyContentIsInTheReport) {
        String css = ReadUiScreen("assets/ui/settings.css");
        ASSERT_NO_FATAL_FAILURE(Replace(css, "#Ok       {", "#Ok       { justify-content: center;"));
        String xml;
        const UIWebReport report = CompileScreen(css, xml);
        ASSERT_EQ(report.issues.size(), 1u) << DescribeIssues(report).c_str();
        EXPECT_NE(report.issues[0].message.find("justify-content"), String::npos) << report.issues[0].message.c_str();
    }

    TEST_F(GuideExamplesTest, ASliderRowOfTwentyUnitsIsFoundOnTheMatrix) {
        const TestTempFolder temp("BagiGuideExamples");
        String css = ReadUiScreen("assets/ui/settings.css");
        ASSERT_NO_FATAL_FAILURE(Replace(css, "#Volume   { flex: 1; height: 56px; }", "#Volume   { flex: 1; height: 20px; }"));
        temp.Write("settings.css", css);
        const String screen = temp.Write("settings.ui.html", ReadUiScreen(kScreenSource));

        ASSERT_NO_FATAL_FAILURE(LoadUiScreen(screen));
        const String problems = ProblemsOnMatrix();
        EXPECT_NE(problems.find("Volume"), String::npos) << problems.c_str();
    }

    TEST_F(GuideExamplesTest, AnOverrideOfAComponentThePrefabLacksDropsTheWholeInstance) {
        String scene = ReadFile(PrefabFolder() / "config" / "SceneConfig.xml");
        ASSERT_NO_FATAL_FAILURE(Replace(scene, "<component type=\"TextComponent\" text=\"One\" />", "<component type=\"SpriteRendererComponent\" spriteId=\"None\" />"));
        LogRecorder log;
        ASSERT_NO_FATAL_FAILURE(LoadPrefabScene(scene));
        LogEvent::Flush();

        const Vector<String> errors = log.Messages(LogLevel::Error, {});
        ASSERT_EQ(errors.size(), 1u) << (errors.empty() ? "" : errors[0].c_str());
        EXPECT_EQ(PrefabNode("BadgeOne"_intern), nullptr);
        EXPECT_NE(PrefabNode("Sky"_intern), nullptr);
        EXPECT_NE(PrefabNode("BadgeTwo"_intern), nullptr);
        EXPECT_NE(PrefabNode("BadgeThree"_intern), nullptr) << "the rest of the scene loads";
    }

}  // namespace BECore::Tests
