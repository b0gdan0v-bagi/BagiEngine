#include <BECore/Animation/TweenManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIInputClicker.h>
#include <BECore/UI/UITextComponent.h>
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

        constexpr float kFrame = 1.0f / 32.0f;  // a power of two: the sums of frame times are exact
        constexpr int kMaxFrames = 1000;

        bool SameDraw(const eastl::vector<DrawCall>& a, const eastl::vector<DrawCall>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                const DrawCall& x = a[i];
                const DrawCall& y = b[i];
                if (x.kind != y.kind || x.rect.x != y.rect.x || x.rect.y != y.rect.y || x.rect.w != y.rect.w || x.rect.h != y.rect.h || x.color.r != y.color.r || x.color.g != y.color.g ||
                    x.color.b != y.color.b || x.color.a != y.color.a) {
                    return false;
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

    /// The buttons of the shipped file are composed elements (migrated from `UIButton` by tools/migrate, step 1 to 2): a plain element with a
    /// text component and a clicker that holds the action
    TEST_F(Match3UiTest, TheShippedButtonsAreComposedElementsWithTheirAction) {
        struct Button {
            const char* scene;
            const char* node;
            const char* name;
            const char* action;
        };
        for (const Button& button : {Button{"MainMenu", "Menu", "Play", "Match3Play"}, Button{"MainMenu", "Menu", "Quit", "Match3Quit"}, Button{"Match3Scene", "Hud", "Reset", "Match3Reset"},
                                     Button{"Match3Scene", "Hud", "Menu", "Match3Menu"}}) {
            auto canvas = Canvas(button.scene, button.node);
            ASSERT_TRUE(canvas) << button.name;
            UIElement* element = canvas->GetRoot().FindByName(PoolString::Intern(button.name));
            ASSERT_NE(element, nullptr) << button.name;
            const UIInputClicker* clicker = element->GetComponent<UIInputClicker>();
            ASSERT_NE(clicker, nullptr) << button.name;
            EXPECT_EQ(clicker->action, PoolString::Intern(button.action)) << button.name;
            EXPECT_NE(element->GetComponent<UITextComponent>(), nullptr) << button.name;
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
        auto* score = canvas->GetRoot().FindByName(PoolString::Intern("Score"))->GetComponent<UITextComponent>();
        auto* seed = canvas->GetRoot().FindByName(PoolString::Intern("Seed"))->GetComponent<UITextComponent>();
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

    // ---- the animation of a swap --------------------------------------------------------------------------------------------

    class Match3AnimationTest : public Match3UiTest {
    protected:
        void SetUp() override {
            Match3UiTest::SetUp();
            Activate("Match3Scene");
            Draw();  // the first draw fits the view to the output, which the clicks need
        }

        /// The click of a person on both cells of a swap that scores
        eastl::pair<Cell, Cell> ClickALegalSwap() {
            const auto swaps = Board().GetModel().GetLegalSwaps();
            EXPECT_FALSE(swaps.empty());
            Board().ClickCell(swaps.front().first);
            Board().ClickCell(swaps.front().second);
            return {swaps.front().first, swaps.front().second};
        }

        /// The centre of @p cell in the units of the scene, from the geometry of the board component
        eastl::pair<float, float> CellCentre(Cell cell) const {
            const Match3BoardComponent& board = Board();
            return {board._originX + (static_cast<float>(cell.col) + 0.5f) * board._cellSize, board._originY + (static_cast<float>(cell.row) + 0.5f) * board._cellSize};
        }

        /// A neighbour pair whose swap makes no match
        eastl::pair<Cell, Cell> FindRejectedSwap() const {
            const Match3GameModel& model = Board().GetModel();
            for (int8_t row = 0; row < kBoardSize; ++row) {
                for (int8_t col = 0; col + 1 < kBoardSize; ++col) {
                    const Cell a{col, row};
                    const Cell b{static_cast<int8_t>(col + 1), row};
                    if (!model.WouldSwapMatch(a, b)) {
                        return {a, b};
                    }
                }
            }
            ADD_FAILURE() << "every swap of the board matches";
            return {};
        }

        /// The recorded draw of the board in its current state
        eastl::vector<DrawCall> Drawn() {
            Draw();
            return _renderer->calls;
        }

        void AdvanceFrames(int frames) {
            for (int i = 0; i < frames; ++i) {
                CoreManager::GetTweens().Advance(kFrame);
            }
        }

        /// Runs the fake clock until the board stops animating (the tween is done)
        void AdvanceUntilDone() {
            for (int i = 0; i < kMaxFrames && Board().IsAnimating(); ++i) {
                CoreManager::GetTweens().Advance(kFrame);
            }
            ASSERT_FALSE(Board().IsAnimating());
        }

        /// The output pixel at which the scene coordinate is: the fit of the view is linear, so it is inverted from its values at 0 and 1
        eastl::pair<float, float> PixelOfScene(float sceneX, float sceneY) {
            const Scene& scene = GetScene("Match3Scene");
            const float x0 = scene.ScreenToSceneX(0.0f);
            const float y0 = scene.ScreenToSceneY(0.0f);
            return {(sceneX - x0) / (scene.ScreenToSceneX(1.0f) - x0), (sceneY - y0) / (scene.ScreenToSceneY(1.0f) - y0)};
        }

        /// The same swap on a model of its own, to compare the final state with
        static Match3GameModel PlayedElsewhere(uint32_t seed, eastl::pair<Cell, Cell> swap) {
            Match3GameModel model;
            model.Reset(seed);
            EXPECT_TRUE(model.TrySwap(swap.first, swap.second));
            return model;
        }
    };

    TEST_F(Match3AnimationTest, TwoClicksOnALegalSwapAnimateAndTheModelIsAlreadyFinal) {
        EXPECT_FALSE(Board().IsAnimating());

        const auto swap = ClickALegalSwap();

        EXPECT_TRUE(Board().IsAnimating());
        const Match3GameModel elsewhere = PlayedElsewhere(Board().GetSeed(), swap);
        EXPECT_TRUE(SameBoard(Board().GetModel().GetBoard(), elsewhere.GetBoard())) << "the model does not wait for the picture";
        EXPECT_EQ(Board().GetModel().GetScore(), elsewhere.GetScore());
        EXPECT_GT(Board().GetModel().GetScore(), 0);
        EXPECT_EQ(Board().GetModel().GetVersion(), elsewhere.GetVersion());

        Draw();
        EXPECT_TRUE(Board().IsAnimating()) << "a draw of the same version keeps the run";
        auto score = Canvas("Match3Scene", "Hud")->GetRoot().FindByName(PoolString::Intern("Score"))->GetComponent<UITextComponent>();
        const eastl::string expected = Format("Score: {}", elsewhere.GetScore());
        EXPECT_EQ(score->GetDisplayText(), eastl::string_view(expected.data(), expected.size())) << "the score shows at once";
    }

    TEST_F(Match3AnimationTest, AMidWayDrawDiffersFromTheSettledOneAndTheEndEqualsABoardThatNeverAnimated) {
        ClickALegalSwap();
        const eastl::vector<DrawCall> atStart = Drawn();
        AdvanceFrames(5);  // 5/32 s: the swap slide (4/32 s) is done and the matched gems shrink
        ASSERT_TRUE(Board().IsAnimating());
        const eastl::vector<DrawCall> midWay = Drawn();
        EXPECT_FALSE(SameDraw(atStart, midWay)) << "the sprites moved";

        Board().SkipAnimation();
        EXPECT_FALSE(Board().IsAnimating());
        const eastl::vector<DrawCall> settled = Drawn();
        EXPECT_FALSE(SameDraw(midWay, settled));

        // The same seed and swap with the animation switched off draw the same picture
        Emit("Match3Reset");
        Board()._animate = false;
        ClickALegalSwap();
        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_TRUE(SameDraw(settled, Drawn()));
    }

    TEST_F(Match3AnimationTest, TheRunEndsOnItsOwnWithTheSettledPicture) {
        ClickALegalSwap();
        Board().SkipAnimation();
        const eastl::vector<DrawCall> settled = Drawn();

        Emit("Match3Reset");
        ClickALegalSwap();
        AdvanceUntilDone();
        EXPECT_TRUE(SameDraw(settled, Drawn()));
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, ChangesThatNoClickStartedAreDrawnAtOnce) {
        ScoreSomething();  // TrySwap of a test
        Draw();
        EXPECT_FALSE(Board().IsAnimating());

        Emit("Match3Reset");  // the Reset button
        Draw();
        EXPECT_FALSE(Board().IsAnimating());

        Board().GetModel().Reset(7);  // what the control API does
        Draw();
        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, AChangeOfTheModelDuringTheRunSnapsAtTheNextDraw) {
        ClickALegalSwap();
        ASSERT_TRUE(Board().IsAnimating());
        const auto swaps = Board().GetModel().GetLegalSwaps();
        ASSERT_FALSE(swaps.empty());

        ASSERT_TRUE(Board().GetModel().TrySwap(swaps.front().first, swaps.front().second));  // the control API plays a move
        Draw();

        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, TheResetAndMenuButtonsEndTheRun) {
        ClickALegalSwap();
        ASSERT_TRUE(Board().IsAnimating());
        Emit("Match3Reset");
        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);

        ClickALegalSwap();
        ASSERT_TRUE(Board().IsAnimating());
        Emit("Match3Menu");
        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, ASwitchedOffAnimationShowsTheResultAtOnce) {
        Board()._animate = false;
        ClickALegalSwap();

        EXPECT_FALSE(Board().IsAnimating());
        EXPECT_GT(Board().GetModel().GetScore(), 0);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, AClickDuringTheRunFinishesItAndIsHandled) {
        ClickALegalSwap();
        AdvanceFrames(2);
        ASSERT_TRUE(Board().IsAnimating());
        const Cell other{3, 4};
        ASSERT_FALSE(Board().GetModel().HasSelection());

        Board().ClickCell(other);

        EXPECT_FALSE(Board().IsAnimating()) << "selecting is not a swap";
        ASSERT_TRUE(Board().GetModel().HasSelection());
        EXPECT_EQ(*Board().GetModel().GetSelected(), other);
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

    TEST_F(Match3AnimationTest, AClickThatSwapsDuringTheRunStartsTheNextRun) {
        ClickALegalSwap();
        AdvanceFrames(2);
        ASSERT_TRUE(Board().IsAnimating());
        const auto swaps = Board().GetModel().GetLegalSwaps();
        ASSERT_FALSE(swaps.empty());
        const uint64_t version = Board().GetModel().GetVersion();

        Board().ClickCell(swaps.front().first);
        EXPECT_FALSE(Board().IsAnimating());
        Board().ClickCell(swaps.front().second);

        EXPECT_EQ(Board().GetModel().GetVersion(), version + 1);
        EXPECT_TRUE(Board().IsAnimating());
        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 1u) << "one run at a time";
    }

    TEST_F(Match3AnimationTest, ARejectedSwapByTwoClicksAnimatesAndEndsOnTheUnchangedBoard) {
        const auto [a, b] = FindRejectedSwap();
        const Match3Board before = Board().GetModel().GetBoard();
        const uint64_t version = Board().GetModel().GetVersion();
        const eastl::vector<DrawCall> settled = Drawn();

        Board().ClickCell(a);
        EXPECT_FALSE(Board().IsAnimating()) << "a selection is not a swap";
        Board().ClickCell(b);

        EXPECT_TRUE(Board().IsAnimating());
        EXPECT_EQ(Board().GetModel().GetVersion(), version) << "nothing was committed";
        EXPECT_TRUE(SameBoard(Board().GetModel().GetBoard(), before));
        AdvanceFrames(2);
        EXPECT_FALSE(SameDraw(settled, Drawn())) << "the two gems are on their way";
        EXPECT_TRUE(Board().IsAnimating());

        AdvanceUntilDone();
        EXPECT_TRUE(SameBoard(Board().GetModel().GetBoard(), before));
        EXPECT_TRUE(SameDraw(settled, Drawn()));
        EXPECT_EQ(Board().GetModel().GetVersion(), version);
    }

    TEST_F(Match3AnimationTest, AClickOutsideTheBoardWithASelectionChangesNothingAndDoesNotAnimate) {
        const Match3Board before = Board().GetModel().GetBoard();
        const uint64_t version = Board().GetModel().GetVersion();
        Board().ClickCell(Cell{0, 0});
        ASSERT_TRUE(Board().GetModel().HasSelection());

        for (const Cell outside : {Cell{-1, 0}, Cell{0, -1}, Cell{kBoardSize, 0}, Cell{0, kBoardSize}}) {
            Board().ClickCell(outside);

            EXPECT_FALSE(Board().IsAnimating());
            EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
            EXPECT_EQ(Board().GetModel().GetVersion(), version);
            ASSERT_TRUE(Board().GetModel().HasSelection());
            EXPECT_EQ(*Board().GetModel().GetSelected(), (Cell{0, 0}));
            EXPECT_TRUE(SameBoard(Board().GetModel().GetBoard(), before));
        }
    }

    TEST_F(Match3AnimationTest, AClickAtThePixelOfACellSelectsThatCell) {
        const Match3Board& board = Board().GetModel().GetBoard();
        for (const Cell cell : {Cell{0, 0}, Cell{7, 7}, Cell{3, 5}, Cell{6, 1}}) {
            ASSERT_NE(board.Get(cell), GemType::None);
            const auto [centreX, centreY] = CellCentre(cell);
            const auto [x, y] = PixelOfScene(centreX, centreY);

            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));

            ASSERT_TRUE(Board().GetModel().HasSelection());
            EXPECT_EQ(*Board().GetModel().GetSelected(), cell);
            Board().GetModel().OnCellClicked(cell);  // deselect
        }
        EXPECT_FALSE(Board().IsAnimating());
    }

    TEST_F(Match3AnimationTest, TwoMouseClicksOnTheScreenPlayTheSwap) {
        const auto swaps = Board().GetModel().GetLegalSwaps();
        ASSERT_FALSE(swaps.empty());
        for (const Cell cell : {swaps.front().first, swaps.front().second}) {
            const auto [centreX, centreY] = CellCentre(cell);
            const auto [x, y] = PixelOfScene(centreX, centreY);
            CoreManager::GetUIInput().DispatchMouseButtonDown(Button(x, y, true));
        }

        EXPECT_GT(Board().GetModel().GetScore(), 0);
        EXPECT_TRUE(Board().IsAnimating());
    }

    TEST_F(Match3AnimationTest, DestroyingTheSceneMidRunLeavesNoTween) {
        ClickALegalSwap();
        AdvanceFrames(1);
        ASSERT_TRUE(Board().IsAnimating());
        ASSERT_EQ(CoreManager::GetTweens().GetActiveCount(), 1u);

        EngineEnvironment::UseSceneManager({});
        _manager.Reset();

        EXPECT_EQ(CoreManager::GetTweens().GetActiveCount(), 0u);
    }

}  // namespace BECore::Match3::Tests
