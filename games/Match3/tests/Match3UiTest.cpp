#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILabel.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <Events/InputEvents.h>
#include <Match3/Components/Match3BoardComponent.h>
#include <Match3/Game/Match3Board.h>
#include <Match3/Match3Game.h>
#include <gtest/gtest.h>

namespace BECore::Match3::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr float kViewWidth = 712.0f;
        constexpr float kViewHeight = 672.0f;
        const Color kResetColor{40, 90, 60, 255};

        /// The scenes the game ships, from the file next to the sources: a game's own content is not mounted in the test environment
        std::filesystem::path ShippedSceneConfig() {
            return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
        }

        bool SameBoard(const Match3Board& a, const Match3Board& b) {
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col < kBoardSize; ++col) {
                    if (a.Get(Cell{col, row}) != b.Get(Cell{col, row})) {
                        return false;
                    }
                }
            }
            return true;
        }

        class QuitRecorder : public SubscriptionHolder {
        public:
            QuitRecorder() {
                Subscribe<ApplicationEvents::QuitEvent, &QuitRecorder::OnQuit>(this);
            }
            int quits = 0;

        private:
            void OnQuit(const ApplicationEvents::QuitEvent&) {
                ++quits;
            }
        };

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

    }  // namespace

    class Match3UiTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 1280.0f;
            _renderer->height = 720.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // the menu and the board switch scenes through CoreManager
        }

        void TearDown() override {
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        IntrusivePtr<UICanvasComponent> Canvas(const char* sceneName, const char* nodeName) const {
            for (const auto& node : GetScene(sceneName).GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        Match3BoardComponent& Board() const {
            auto* board = Match3BoardComponent::GetActive();
            EXPECT_NE(board, nullptr);
            return *board;
        }

        void Activate(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
        }

        [[nodiscard]] PoolString ActiveScene() const {
            return _manager->GetActiveScene()->GetName();
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        void Emit(const char* action) {
            InputEvents::ActionEvent::Emit(PoolString::Intern(action));
        }

        /// A swap that scores, so the model has something to show and to lose
        void ScoreSomething() {
            auto& model = Board().GetModel();
            const auto swaps = model.GetLegalSwaps();
            ASSERT_FALSE(swaps.empty());
            ASSERT_TRUE(model.TrySwap(swaps.front().first, swaps.front().second));
            ASSERT_GT(model.GetScore(), 0);
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
    };

    // ---- the shipped screens -----------------------------------------------------------------------------------------------

    TEST_F(Match3UiTest, TheGameOpensOnTheMenu) {
        EXPECT_EQ(ActiveScene(), PoolString::Intern("MainMenu")) << "the first scene of the file";
        EXPECT_EQ(New<Match3Game>()->GetSceneName(), PoolString::Intern("MainMenu"));
    }

    TEST_F(Match3UiTest, TheMenuPassesTheSizeMatrix) {
        auto canvas = Canvas("MainMenu", "Menu");
        ASSERT_TRUE(canvas);
        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(canvas->GetLayout(), options);

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(Match3UiTest, TheHudPassesTheSizeMatrixWithTheLongestValues) {
        auto canvas = Canvas("Match3Scene", "Hud");
        ASSERT_TRUE(canvas);
        CoreManager::GetUIVars().Set("score"_intern, 99999999);
        CoreManager::GetUIVars().Set("seed"_intern, 4294967295u);

        canvas->UpdateLayout(1280.0f, 720.0f);  // the reflected fields of the component reach the layout on its first update
        UILayoutContext base;
        base.sceneViewWidth = GetScene("Match3Scene").GetViewWidth();
        base.sceneViewHeight = GetScene("Match3Scene").GetViewHeight();
        ASSERT_FLOAT_EQ(base.sceneViewWidth, kViewWidth);
        ASSERT_FLOAT_EQ(base.sceneViewHeight, kViewHeight);

        UILayoutValidatorOptions options;
        options.minTouchSize = CoreManager::GetUIEnvironment().GetSettings().minTouchSize;
        const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, base);

        EXPECT_TRUE(failures.empty()) << DescribeMatrixFailures(failures).c_str();
    }

    TEST_F(Match3UiTest, TheHudStaysOutsideTheBoardInEveryShape) {
        Activate("Match3Scene");
        auto canvas = Canvas("Match3Scene", "Hud");
        ASSERT_TRUE(canvas);
        const float boardTop = 80.0f - 4.0f;  // the frame of the board, in view units
        const float boardBottom = 80.0f + 512.0f + 4.0f;

        for (const auto& size : {std::pair{1280.0f, 720.0f}, std::pair{720.0f, 1280.0f}, std::pair{1024.0f, 768.0f}, std::pair{800.0f, 800.0f}, std::pair{2560.0f, 1080.0f}}) {
            _renderer->width = size.first;
            _renderer->height = size.second;
            Draw();
            UIElement& root = canvas->GetRoot();
            for (const char* name : {"Score", "Buttons"}) {
                const UIElement* element = root.FindByName(PoolString::Intern(name));
                ASSERT_NE(element, nullptr);
                EXPECT_LE(element->rect.y + element->rect.h, boardTop + 0.01f) << name << " at " << size.first << "x" << size.second;
            }
            const UIElement* seed = root.FindByName(PoolString::Intern("Seed"));
            ASSERT_NE(seed, nullptr);
            EXPECT_GE(seed->rect.y, boardBottom - 0.01f) << "Seed at " << size.first << "x" << size.second;
        }
    }

    // ---- what the buttons do -----------------------------------------------------------------------------------------------

    TEST_F(Match3UiTest, PlayStartsANewGameInTheGameScene) {
        Activate("MainMenu");
        ScoreSomething();
        const uint64_t before = Board().GetModel().GetVersion();

        Emit("Match3Play");

        EXPECT_EQ(ActiveScene(), PoolString::Intern("Match3Scene"));
        EXPECT_EQ(Board().GetModel().GetScore(), 0);
        EXPECT_GT(Board().GetModel().GetVersion(), before);
    }

    TEST_F(Match3UiTest, QuitEndsTheApplication) {
        Activate("MainMenu");
        QuitRecorder quit;

        Emit("Match3Quit");

        EXPECT_EQ(quit.quits, 1);
    }

    TEST_F(Match3UiTest, ResetStartsTheBoardAgainWithTheConfiguredSeed) {
        Activate("Match3Scene");
        const Match3Board fresh = Board().GetModel().GetBoard();
        ScoreSomething();
        ASSERT_FALSE(SameBoard(Board().GetModel().GetBoard(), fresh)) << "a scoring swap changes the board";

        Emit("Match3Reset");

        EXPECT_EQ(Board().GetModel().GetScore(), 0);
        EXPECT_TRUE(SameBoard(Board().GetModel().GetBoard(), fresh)) << "the same seed gives the same board";
        EXPECT_EQ(ActiveScene(), PoolString::Intern("Match3Scene"));
    }

    TEST_F(Match3UiTest, MenuLeadsBackToTheMenu) {
        Activate("Match3Scene");

        Emit("Match3Menu");

        EXPECT_EQ(ActiveScene(), PoolString::Intern("MainMenu"));
    }

    TEST_F(Match3UiTest, ActionsOfTheOtherSceneAreIgnored) {
        Activate("MainMenu");
        ScoreSomething();
        const int score = Board().GetModel().GetScore();
        QuitRecorder quit;

        Emit("Match3Reset");  // the board is not in the active scene
        Emit("Match3Menu");
        EXPECT_EQ(Board().GetModel().GetScore(), score);
        EXPECT_EQ(ActiveScene(), PoolString::Intern("MainMenu"));

        Activate("Match3Scene");
        Emit("Match3Play");  // the menu is not in the active scene
        Emit("Match3Quit");
        EXPECT_EQ(ActiveScene(), PoolString::Intern("Match3Scene"));
        EXPECT_EQ(quit.quits, 0);
    }

    // ---- the HUD on screen -------------------------------------------------------------------------------------------------

    TEST_F(Match3UiTest, TheHudShowsTheScoreAndTheSeed) {
        Activate("Match3Scene");
        Draw();
        auto canvas = Canvas("Match3Scene", "Hud");
        auto* score = canvas->GetRoot().FindByName(PoolString::Intern("Score"))->Cast<UILabel>();
        auto* seed = canvas->GetRoot().FindByName(PoolString::Intern("Seed"))->Cast<UILabel>();
        ASSERT_NE(score, nullptr);
        ASSERT_NE(seed, nullptr);
        EXPECT_EQ(score->GetDisplayText(), "Score: 0");
        EXPECT_EQ(seed->GetDisplayText(), "seed 1337");

        ScoreSomething();
        Draw();
        const eastl::string expected = Format("Score: {}", Board().GetModel().GetScore());
        EXPECT_EQ(score->GetDisplayText(), eastl::string_view(expected.data(), expected.size()));
    }

    TEST_F(Match3UiTest, ClickingTheResetButtonOnTheScreenResetsTheBoard) {
        Activate("Match3Scene");
        ScoreSomething();
        Draw();

        // The button is where the draw put it: find its fill in output pixels and click the middle
        const DrawCall* fill = nullptr;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Rect && call.color.r == kResetColor.r && call.color.g == kResetColor.g && call.color.b == kResetColor.b) {
                fill = &call;
            }
        }
        ASSERT_NE(fill, nullptr);
        const float x = fill->rect.x + fill->rect.w * 0.5f;
        const float y = fill->rect.y + fill->rect.h * 0.5f;

        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true))) << "the button takes the click";
        EXPECT_GT(Board().GetModel().GetScore(), 0) << "a press alone does nothing";
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseButtonUp(Button(x, y, false)));

        EXPECT_EQ(Board().GetModel().GetScore(), 0);
    }

}  // namespace BECore::Match3::Tests
