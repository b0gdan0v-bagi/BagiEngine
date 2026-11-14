#include "StressSceneFixture.h"

#include <BECore/Config/XmlConfig.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Stress/Components/StressUiComponent.h>
#include <gtest/gtest.h>

/**
 * Coverage of StressUiComponent on the shipped scene file and the shipped string table, drawn on a RecordingRenderer: a label per count on one
 * canvas, English and Russian both drawn, a language switch that rewords the `{#key}` labels and no other, a field that grows and shrinks from
 * the tail, and a game that is not active builds nothing.
 */
namespace BECore::Stress::Tests {

    using BECore::Tests::DrawCall;

    namespace {

        constexpr uint32_t kCount = 200;
        constexpr float kOutputWidth = 1920.0f;
        constexpr float kOutputHeight = 1080.0f;

        /// The table the app ships, from the file next to the sources: the app's own content is not mounted in the test environment
        void LoadShippedStrings() {
            const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "UIStrings.xml";
            auto config = XmlConfig::Create();
            ASSERT_TRUE(config.LoadFromFile(path)) << path.string();
            ASSERT_TRUE(CoreManager::GetUIStrings().LoadFromXml(config.GetRoot()).empty());
        }

        eastl::string Shown(const UIElement& label) {
            const UITextComponent* text = label.GetComponent<UITextComponent>();
            EXPECT_NE(text, nullptr);
            const StringView shown = text->GetDisplayText();
            return eastl::string(shown.data(), shown.size());
        }

        eastl::vector<eastl::string> ShownByAll(const UIElement& root) {
            eastl::vector<eastl::string> all;
            for (const auto& label : root.children) {
                all.push_back(Shown(*label));
            }
            return all;
        }

    }  // namespace

    class StressUiTest : public StressSceneFixture {
    protected:
        void SetUp() override {
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
            LoadShippedStrings();
        }

        void TearDown() override {
            StressSceneFixture::TearDown();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();  // global: the next test starts with none
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void StartUi(uint32_t count = kCount) {
            StartGame({"--stress-scene=ui", Format("--stress-count={}", count).c_str()});
            ASSERT_EQ(_manager->GetActiveScene()->GetName(), "StressUi"_intern);
            _renderer->width = kOutputWidth;
            _renderer->height = kOutputHeight;
        }

        SceneNode& Field() const {
            return StressSceneFixture::Field("StressUi");
        }

        UICanvasComponent& Canvas() const {
            auto canvas = Field().GetComponent<UICanvasComponent>();
            EXPECT_TRUE(canvas);
            return *canvas;
        }

        StressUiComponent& Component() const {
            auto component = Field().GetComponent<StressUiComponent>();
            EXPECT_TRUE(component);
            return *component;
        }

        UIElement& Root() const {
            return Canvas().GetRoot();
        }
    };

    TEST_F(StressUiTest, ALabelPerCountOnTheOneCanvas) {
        StartUi();
        Update();

        EXPECT_EQ(Component().GetLabelCount(), size_t{kCount});
        EXPECT_EQ(Root().children.size(), size_t{kCount});
        for (const auto& label : Root().children) {
            EXPECT_TRUE(label->GetComponent<UITextComponent>());
        }
    }

    TEST_F(StressUiTest, TheCountOfTheCommandLineIsWhatTheFirstFrameShows) {
        StartUi(33);
        Update();

        EXPECT_EQ(Root().children.size(), 33u);
    }

    TEST_F(StressUiTest, DrawsGrowWithTheCount) {
        StartUi(20);
        Update();
        Draw();
        const size_t few = _renderer->calls.size();
        EXPECT_GT(few, 0u);

        SetCount(200);
        Update();
        Draw();
        EXPECT_GT(_renderer->calls.size(), few * 2);
    }

    TEST_F(StressUiTest, AnEnglishLabelAndARussianOneBothDraw) {
        StartUi(2);  // label 0 is a key (English words) in the first row, label 1 is Russian in the second
        Update();
        Draw();

        const float half = kOutputHeight / 2.0f;
        size_t firstRow = 0;
        size_t secondRow = 0;
        for (const DrawCall& call : _renderer->calls) {
            (call.rect.y < half ? firstRow : secondRow)++;
        }
        EXPECT_GT(firstRow, 0u);
        EXPECT_GT(secondRow, 0u) << "the Russian label drew no glyph: the font lacks Cyrillic or the layout dropped it";
    }

    TEST_F(StressUiTest, TheLabelsShowTheirTexts) {
        StartUi(40);
        Update();

        for (uint32_t i = 0; i < 40; ++i) {
            const UiLabelKind kind = KindOf(i);
            const eastl::string shown = Shown(*Root().children[i]);
            if (kind == UiLabelKind::English || kind == UiLabelKind::Russian) {
                EXPECT_EQ(shown, eastl::string(TextOf(i).data(), TextOf(i).size())) << i;
            } else {
                EXPECT_EQ(shown.find('{'), eastl::string::npos) << "expanded: " << i << " " << shown.c_str();
            }
        }
        EXPECT_EQ(Shown(*Root().children[4]), "Objects: 40");
        EXPECT_EQ(Shown(*Root().children[0]), "Frame budget");
    }

    TEST_F(StressUiTest, ALanguageSwitchRewordsTheKeyLabelsAndNoOther) {
        StartUi(64);
        Update();
        const eastl::vector<eastl::string> english = ShownByAll(Root());

        StressGame::GetActive()->GetState().SetLanguage(StressLanguage::Ru);
        Update();
        const eastl::vector<eastl::string> russian = ShownByAll(Root());

        ASSERT_EQ(english.size(), russian.size());
        size_t changed = 0;
        for (uint32_t i = 0; i < english.size(); ++i) {
            if (KindOf(i) == UiLabelKind::Key) {
                EXPECT_NE(english[i], russian[i]) << i;
                ++changed;
            } else {
                EXPECT_EQ(english[i], russian[i]) << i;
            }
        }
        EXPECT_EQ(changed, 64u / kUiKeyEvery);
        EXPECT_EQ(russian[0], "Бюджет кадра");

        StressGame::GetActive()->GetState().SetLanguage(StressLanguage::En);
        Update();
        EXPECT_EQ(ShownByAll(Root()), english);
    }

    TEST_F(StressUiTest, ALanguageSwitchLeavesTheLabelsAlone) {
        StartUi(64);
        Update();
        eastl::vector<const UIElement*> before;
        for (const auto& label : Root().children) {
            before.push_back(label.Get());
        }

        StressGame::GetActive()->GetState().SetLanguage(StressLanguage::Ru);
        Update();

        ASSERT_EQ(Root().children.size(), before.size());
        for (size_t i = 0; i < before.size(); ++i) {
            EXPECT_EQ(Root().children[i].Get(), before[i]) << i;
        }
    }

    TEST_F(StressUiTest, TheLanguageOfTheCommandLineIsTheFirstFramesLanguage) {
        StartGame({"--stress-scene=ui", "--stress-count=16", "--stress-language=ru"});
        Update();

        EXPECT_EQ(Shown(*Root().children[0]), "Бюджет кадра");
        EXPECT_EQ(CoreManager::GetUIStrings().GetLanguage(), "ru"_intern);
    }

    TEST_F(StressUiTest, GrowAppendsAndShrinkRemovesTheTailKeepingTheFirstLabels) {
        StartUi(100);
        Update();
        eastl::vector<const UIElement*> first;
        for (size_t i = 0; i < 10; ++i) {
            first.push_back(Root().children[i].Get());
        }

        SetCount(250);
        Update();
        EXPECT_EQ(Root().children.size(), 250u);
        EXPECT_EQ(Component().GetLabelCount(), 250u);

        SetCount(40);
        Update();
        EXPECT_EQ(Root().children.size(), 40u);
        EXPECT_EQ(Component().GetLabelCount(), 40u);
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_EQ(Root().children[i].Get(), first[i]) << i;
        }
    }

    TEST_F(StressUiTest, ACountChangeMovesTheKeptLabelsOntoTheNewGrid) {
        StartUi(100);
        Update();
        const UiGrid before = MakeGrid(100, FieldView{1920.0f, 1080.0f});
        EXPECT_EQ(Root().children[1]->style.rect.width.value, before.cellWidth);

        SetCount(300);
        Update();
        const UiGrid after = MakeGrid(300, FieldView{1920.0f, 1080.0f});
        EXPECT_NE(before.cellWidth, after.cellWidth);
        EXPECT_EQ(Root().children[1]->style.rect.width.value, after.cellWidth);
        EXPECT_EQ(Root().children[299]->style.rect.x.value, CellOf(299, after).x);
    }

    TEST_F(StressUiTest, TheVariablesFollowTheCount) {
        StartUi(40);
        Update();
        EXPECT_EQ(Shown(*Root().children[4]), "Objects: 40");

        SetCount(90);
        Update();
        EXPECT_EQ(Shown(*Root().children[4]), "Objects: 90");
        EXPECT_EQ(Shown(*Root().children[15]), "Последняя: 89");
    }

    TEST_F(StressUiTest, CountZeroShowsNothingAndComesBack) {
        StartUi(30);
        Update();
        SetCount(0);
        Update();
        Draw();
        EXPECT_TRUE(Root().children.empty());
        EXPECT_TRUE(_renderer->calls.empty());

        SetCount(30);
        Update();
        Draw();
        EXPECT_EQ(Root().children.size(), 30u);
        EXPECT_FALSE(_renderer->calls.empty());
    }

    TEST_F(StressUiTest, AGameThatIsNotActiveBuildsNothing) {
        StartUi();
        GameManager::GetInstance().Shutdown();
        Update();

        EXPECT_TRUE(Root().children.empty());
    }

    TEST_F(StressUiTest, AnotherActiveSceneDoesNotBuildTheField) {
        StartUi();
        _manager->SetActiveScene("StressSprites"_intern);
        Update();

        EXPECT_TRUE(Root().children.empty());
    }

    TEST_F(StressUiTest, EveryKeyOfTheLabelsIsInBothLanguages) {
        UIStrings& strings = CoreManager::GetUIStrings();
        for (uint32_t i = 0; i < kUiPoolSize * kUiKeyEvery; i += kUiKeyEvery) {
            const StringView key = KeyOf(i);
            for (const char* language : {"en", "ru"}) {
                ASSERT_TRUE(strings.SetLanguage(PoolString::Intern(language)));
                const auto text = strings.Find(key);
                ASSERT_TRUE(text.has_value()) << key.data() << " in " << language;
                EXPECT_FALSE(text->empty());
            }
        }
        EXPECT_TRUE(strings.GetMissing().empty());
    }

}  // namespace BECore::Stress::Tests
