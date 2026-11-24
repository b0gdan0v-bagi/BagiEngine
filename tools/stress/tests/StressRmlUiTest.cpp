// The scene `rmlui`: it exists only in a build with the RmlUi host (STRESS_WITH_RMLUI). The settings and the game first, then the scene itself.

#include <BECore/Application/CommandLine.h>
#include <BECore/Control/Json.h>
#include <BECore/Control/JsonDeserializer.h>
#include <BECore/Control/JsonSerializer.h>
#include <BECore/File/File.h>
#include <BECore/File/Path.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/UIInput.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Profiler/FrameStats/FrameStatsCollector.h>
#include <BECore/Reflection/MethodCall.h>
#include <BECore/Reflection/TypedRef.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Stress/RmlUi/RmlUiField.h>
#include <Stress/RmlUi/StressRmlUiComponent.h>
#include <Stress/StressGame.h>
#include <Stress/StressGameControl.h>
#include <Stress/StressNames.h>
#include <Stress/StressSettings.h>
#include <gtest/gtest.h>
#include <tests/StressSceneFixture.h>

#ifdef STRESS_WITH_RMLUI
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <RmlUiHost/RmlUiRuntime.h>
#endif

namespace BECore::Stress::Tests {

    namespace {

        std::expected<StressSettings, String> Parse(const Vector<String>& args) {
            CommandLine line;
            line.Set(args);
            return ParseStressSettings(line);
        }

    }  // namespace

#ifdef STRESS_WITH_RMLUI

    namespace {

        class StressRmlUiSceneTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                BECore::Tests::EngineEnvironment::EnsureCoreServices();
            }

            void TearDown() override {
                CoreManager::GetCommandLine().Set({});
            }
        };

    }  // namespace

    TEST_F(StressRmlUiSceneTest, TheFlagNamesTheSceneInAnyCase) {
        for (const char* flag : {"--stress-scene=rmlui", "--stress-scene=RmlUi"}) {
            const auto settings = Parse({flag});
            ASSERT_TRUE(settings.has_value()) << flag;
            EXPECT_EQ(settings->scene, StressScene::RmlUi) << flag;
        }
        EXPECT_EQ(ToFlagValue(StressScene::RmlUi), "rmlui");
        EXPECT_EQ(ParseScene("RmlUi"), StressScene::RmlUi);
    }

    TEST_F(StressRmlUiSceneTest, TheGameShowsTheSceneOfTheFlag) {
        const Vector<String> args = {"--stress-scene=rmlui"};
        CoreManager::GetCommandLine().Set(args);
        const auto game = New<StressGame>();
        EXPECT_EQ(game->GetState().GetScene(), StressScene::RmlUi);
        EXPECT_EQ(game->GetSceneName(), StressNames::rmlui);
    }

    TEST_F(StressRmlUiSceneTest, TheControlApiListsAndSetsTheScene) {
        StressState state;
        int sceneChanges = 0;
        StressGameControl control(state, [&] { ++sceneChanges; });

        const auto listed = control.GetState();
        ASSERT_TRUE(listed.has_value());
        EXPECT_EQ((*listed)["scenes"], (Json::array({"sprites", "entities", "ui", "rmlui"})));

        Json action = Json::object();
        action["type"] = "set_scene";
        action["scene"] = "rmlui";
        ASSERT_TRUE(control.PerformAction(action).has_value());
        EXPECT_EQ(state.GetScene(), StressScene::RmlUi);
        EXPECT_EQ(sceneChanges, 1);
    }

    TEST_F(StressRmlUiSceneTest, TheReloadIsARequestTheSceneServesAndEveryCallCountsOnce) {
        const auto game = New<StressGame>();
        EXPECT_EQ(game->GetRmlUiReloadRequests(), 0u);

        const TypedRef ref = TypedRef::Of(*game);
        ASSERT_TRUE(ref);
        for (uint32_t call = 1; call <= 2; ++call) {
            JsonDeserializer reader(Json::object());
            JsonSerializer writer;
            ASSERT_TRUE(CallMethod(ref, "ReloadRmlUiDocuments", reader, writer).has_value());
            EXPECT_EQ(game->GetRmlUiReloadRequests(), call);
        }
    }

    // The scene on a RecordingRenderer with the RmlUi runtime up, as RmlUiHostTest sets it up (the runtime starts with the first document and
    // stops with the last one, so a test needs no setup of its own)
    namespace {

        constexpr float kOutputWidth = 1920.0f;
        constexpr float kOutputHeight = 1080.0f;

        // Inside the scroller of the panel (stress_rmlui.rcss: 360 x 240 dp, 40 dp from the right and bottom of the 1920 x 1080 reference, which is the output)
        constexpr float kOverThePanelX = 1700.0f;
        constexpr float kOverThePanelY = 920.0f;
        // A point of the output that is no part of the scroller (the panel's document covers the output, so something is under it, but nothing scrolls)
        constexpr float kAwayFromThePanelX = 100.0f;
        constexpr float kAwayFromThePanelY = 100.0f;

        constexpr int kScrollFrames = 16;  // RmlUi scrolls a wheel step smoothly over the frames of the game clock that follow

        SDL_MouseWheelEvent Wheel(float x, float y, SDL_MouseWheelDirection direction = SDL_MOUSEWHEEL_NORMAL, float mouseX = kOverThePanelX, float mouseY = kOverThePanelY) {
            SDL_MouseWheelEvent event{};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.x = x;
            event.y = y;
            event.direction = direction;
            event.mouse_x = mouseX;
            event.mouse_y = mouseY;
            return event;
        }

        class StressRmlUiRunTest : public StressSceneFixture {
        protected:
            void TearDown() override {
                LogEvent::Flush();
                EXPECT_EQ(_log.Count(LogLevel::Warning, {}), 0u);
                EXPECT_EQ(_log.Count(LogLevel::Error, {}), 0u);
                StressSceneFixture::TearDown();
                EXPECT_EQ(RmlUiRuntime::GetInstance().GetHolderCount(), 0) << "a document outlived the scene";
            }

            void StartRmlUi(uint32_t count) {
                ASSERT_NO_FATAL_FAILURE(StartGame({"--stress-scene=rmlui", Format("--stress-count={}", count).c_str()}));
                ASSERT_EQ(_manager->GetActiveScene()->GetName(), StressNames::rmlui);
                _renderer->width = kOutputWidth;
                _renderer->height = kOutputHeight;
                // The app's own content is not mounted in the test environment: the panel is read from the sources
                const Path panel = ShippedSceneConfig().Parent().Parent() / "assets" / "ui" / "stress_rmlui.rml";
                ASSERT_TRUE(GetFileInfo(panel).has_value()) << panel.CStr();
                Component().SetPanelSource(PoolString::Intern(panel.View()));
            }

            StressRmlUiComponent& Component() const {
                auto component = Field("StressRmlUi").GetComponent<StressRmlUiComponent>();
                EXPECT_TRUE(component);
                return *component;
            }

            /// One frame as Application::RunFrame makes it: opened, the scene updated and drawn inside it, closed
            void Frame() {
                FrameStatsCollector& stats = CoreManager::GetFrameStats();
                stats.BeginFrame();
                Update();
                Draw();
                stats.EndFrame({}, {});
            }

            void Frames(int count) {
                for (int frame = 0; frame < count; ++frame) {
                    Frame();
                }
            }

            double Counter(PoolString name) const {
                const auto value = CoreManager::GetFrameStats().GetLast()->FindCounter(name);
                EXPECT_TRUE(value.has_value()) << name.ToStringView().data();
                return value.value_or(-1.0);
            }

            SceneNode* FindChild(SceneNode& parent, PoolString name) const {
                for (const auto& child : parent.GetChildren()) {
                    if (child->GetName() == name) {
                        return child.Get();
                    }
                }
                ADD_FAILURE() << "no child " << name.ToStringView().data();
                return nullptr;
            }

            /// The nodes of the generated documents, in order
            Vector<SceneNode*> Documents() const {
                Vector<SceneNode*> nodes;
                if (SceneNode* docs = FindChild(Field("StressRmlUi"), "Docs"_intern)) {
                    for (const auto& child : docs->GetChildren()) {
                        nodes.push_back(child.Get());
                    }
                }
                return nodes;
            }

            RmlUiDocumentComponent& Panel() const {
                SceneNode* node = FindChild(Field("StressRmlUi"), "Panel"_intern);
                auto panel = node != nullptr ? node->GetComponent<RmlUiDocumentComponent>() : IntrusivePtr<RmlUiDocumentComponent>{};
                EXPECT_TRUE(panel);
                return *panel;
            }

            static size_t Textures() {
                return RmlUiRuntime::GetInstance().GetRenderInterface().GetTextureCount();
            }

            double ScrollX() const {
                return Counter(RmlUiCounters::scrollX);
            }
            double ScrollY() const {
                return Counter(RmlUiCounters::scrollY);
            }

            BECore::Tests::LogRecorder _log;
        };

    }  // namespace

    TEST_F(StressRmlUiRunTest, TheSceneHasCountDocumentsAndKeepsTheFirstOnesWhenTheCountMoves) {
        StartRmlUi(5);
        Frames(2);
        EXPECT_GT(_renderer->calls.size(), 0u) << "the documents and the panel draw";
        ASSERT_EQ(Documents().size(), 5u);
        EXPECT_EQ(Component().GetDocumentCount(), 5u);
        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::documents), 5.0);
        const Vector<SceneNode*> first = Documents();

        SetCount(3);
        Frames(2);
        ASSERT_EQ(Documents().size(), 3u);
        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::documents), 3.0);

        SetCount(8);
        Frames(2);
        const Vector<SceneNode*> grown = Documents();
        ASSERT_EQ(grown.size(), 8u);
        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::documents), 8.0);
        for (size_t i = 0; i < 3; ++i) {
            EXPECT_EQ(grown[i], first[i]) << "document " << i << " was made again";
        }
    }

    TEST_F(StressRmlUiRunTest, ALanguageChangeRewordsEveryDocumentAndAnEqualOneChangesNothing) {
        StartRmlUi(4);
        Frames(2);
        auto textOf = [&](size_t index) { return Documents()[index]->GetComponent<RmlUiDocumentComponent>()->GetDocument()->GetInnerRML(); };
        const auto english = textOf(0);
        Rml::ElementDocument* const kept = Documents()[0]->GetComponent<RmlUiDocumentComponent>()->GetDocument();

        StressGame::GetActive()->GetState().SetLanguage(StressLanguage::En);  // the same language
        Frames(2);
        EXPECT_EQ(Documents()[0]->GetComponent<RmlUiDocumentComponent>()->GetDocument(), kept) << "an equal value is not a change";

        StressGame::GetActive()->GetState().SetLanguage(StressLanguage::Ru);
        Frames(2);
        EXPECT_NE(textOf(0), english);
        ASSERT_EQ(Documents().size(), 4u);
    }

    // RmlUi keeps one callback texture per glyph page of the font engine, and the pages are shared by every context that draws that size: a
    // document costs a context and geometry, not a texture of its own. RmlUi releases the textures together, at its shutdown (`RmlUiRuntime`: the last holder), never with one context
    TEST_F(StressRmlUiRunTest, TexturesFollowTheGlyphPagesNotTheDocumentsAndGoWhenRmlUiShutsDown) {
        constexpr uint32_t kMany = 40;
        StartRmlUi(1);
        Frames(2);
        const size_t one = Textures();
        EXPECT_GE(one, kRmlUiSizes) << "one document draws text at every size, each on a page of its own";
        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::textures), static_cast<double>(one));

        SetCount(kMany);
        Frames(2);
        ASSERT_EQ(Documents().size(), kMany);
        EXPECT_LT(Textures(), kMany * kRmlUiSizes) << "the contexts share the pages";
        EXPECT_GE(Textures(), one);

        GameManager::GetInstance().Shutdown();
        BECore::Tests::EngineEnvironment::UseSceneManager({});
        _manager.Reset();
        EXPECT_EQ(RmlUiRuntime::GetInstance().GetHolderCount(), 0);
        EXPECT_EQ(Textures(), 0u) << "RmlUi's shutdown released the textures";
    }

    TEST_F(StressRmlUiRunTest, ASceneSwitchAwayAndBackLeavesNoDocumentOrTextureBehind) {
        StartRmlUi(6);
        Frames(2);
        const size_t textures = Textures();
        ASSERT_GT(textures, 0u);

        _manager->SetActiveScene(StressNames::sprites);
        Frames(2);
        _manager->SetActiveScene(StressNames::rmlui);
        Frames(2);

        EXPECT_EQ(Documents().size(), 6u);
        EXPECT_EQ(Textures(), textures);
    }

    TEST_F(StressRmlUiRunTest, TheWheelScrollsThePanelTheWayItsDocumentedSignsSay) {
        // Documents sit under the panel, so this also shows that their `pointer-events: none` lets the wheel through
        StartRmlUi(20);
        Frames(2);
        ASSERT_DOUBLE_EQ(ScrollX(), 0.0);
        ASSERT_DOUBLE_EQ(ScrollY(), 0.0);
        UIInput& input = CoreManager::GetUIInput();

        // SDL: y negative is toward the user and scrolls down (RmlUi's positive y), x positive scrolls right
        EXPECT_TRUE(input.DispatchMouseWheel(Wheel(0.0f, -1.0f)));
        Frames(kScrollFrames);
        const double down = ScrollY();
        EXPECT_GT(down, 0.0);
        EXPECT_DOUBLE_EQ(ScrollX(), 0.0);

        EXPECT_TRUE(input.DispatchMouseWheel(Wheel(0.0f, 1.0f)));
        Frames(kScrollFrames);
        EXPECT_LT(ScrollY(), down);

        EXPECT_TRUE(input.DispatchMouseWheel(Wheel(1.0f, 0.0f)));
        Frames(kScrollFrames);
        EXPECT_GT(ScrollX(), 0.0);
    }

    TEST_F(StressRmlUiRunTest, AFlippedWheelReversesBothAxes) {
        StartRmlUi(3);
        Frames(2);

        // SDL reports a flipped event with the opposite values: this one is a step to the right and down
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseWheel(Wheel(-1.0f, 1.0f, SDL_MOUSEWHEEL_FLIPPED)));
        Frames(kScrollFrames);
        EXPECT_GT(ScrollX(), 0.0);
        EXPECT_GT(ScrollY(), 0.0);

        const double right = ScrollX();
        const double down = ScrollY();
        EXPECT_TRUE(CoreManager::GetUIInput().DispatchMouseWheel(Wheel(1.0f, -1.0f, SDL_MOUSEWHEEL_FLIPPED)));
        Frames(kScrollFrames);
        EXPECT_LT(ScrollX(), right);
        EXPECT_LT(ScrollY(), down);
    }

    TEST_F(StressRmlUiRunTest, AWheelAwayFromTheScrollerLeavesTheCountersAlone) {
        StartRmlUi(3);
        Frames(2);

        CoreManager::GetUIInput().DispatchMouseWheel(Wheel(1.0f, -1.0f, SDL_MOUSEWHEEL_NORMAL, kAwayFromThePanelX, kAwayFromThePanelY));
        Frames(kScrollFrames);

        EXPECT_DOUBLE_EQ(ScrollX(), 0.0);
        EXPECT_DOUBLE_EQ(ScrollY(), 0.0);
    }

    TEST_F(StressRmlUiRunTest, AReloadRequestMakesThePanelANewDocumentOnceAndKeepsTheGeneratedOnes) {
        StartRmlUi(4);
        Frames(2);
        CoreManager::GetUIInput().DispatchMouseWheel(Wheel(0.0f, -1.0f));
        Frames(kScrollFrames);
        ASSERT_GT(ScrollY(), 0.0);
        ASSERT_DOUBLE_EQ(Counter(RmlUiCounters::reloads), 0.0);
        const auto text = Documents()[1]->GetComponent<RmlUiDocumentComponent>()->GetDocument()->GetInnerRML();
        const Vector<SceneNode*> nodes = Documents();

        StressGame::GetActive()->ReloadRmlUiDocuments();
        Frames(2);

        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::reloads), 1.0);
        EXPECT_DOUBLE_EQ(ScrollY(), 0.0) << "the new document starts at the top";
        EXPECT_DOUBLE_EQ(ScrollX(), 0.0);
        Rml::ElementDocument* const newPanel = Panel().GetDocument();
        EXPECT_NE(newPanel, nullptr);
        EXPECT_EQ(Documents(), nodes) << "the generated documents keep their nodes";
        EXPECT_EQ(Documents()[1]->GetComponent<RmlUiDocumentComponent>()->GetDocument()->GetInnerRML(), text);

        Frames(3);  // no new request: nothing is loaded again
        EXPECT_DOUBLE_EQ(Counter(RmlUiCounters::reloads), 1.0);
        EXPECT_EQ(Panel().GetDocument(), newPanel);
    }

#else

    TEST(StressRmlUiSceneTest, WithoutTheRmlUiHostTheSceneIsRefusedLikeAnyOtherName) {
        const auto settings = Parse({"--stress-scene=rmlui"});
        ASSERT_FALSE(settings.has_value());
        EXPECT_TRUE(settings.error().find("--stress-scene") != String::npos) << settings.error().c_str();
        EXPECT_FALSE(ParseScene("rmlui").has_value());
    }

#endif

}  // namespace BECore::Stress::Tests
