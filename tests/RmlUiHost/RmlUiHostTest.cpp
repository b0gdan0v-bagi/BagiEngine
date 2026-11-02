#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <RmlUi/Core.h>
#include <RmlUiHost/RmlUiDocumentComponent.h>
#include <RmlUiHost/RmlUiRuntime.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr float kTolerance = 0.6f;  // RmlUi lays out in whole pixels at the context's scale

        // "Main": a document on the screen at one pixel per unit. "Fit": a scene with a view size and a document in it. "Scaled": the screen scaled with the output.
        constexpr const char* kScenesXml = R"(
<SceneManager><scenes>
  <scene type="Scene" name="Main"><nodes>
    <node name="Doc" layer="10"><components>
      <component type="RmlUiDocumentComponent" space="Screen" scaleMode="ConstantPixelSize"/>
    </components></node>
    <node name="Menu" layer="20"><components>
      <component type="RmlUiDocumentComponent" space="Screen" scaleMode="ConstantPixelSize" modal="true" visible="false"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Fit" viewWidth="400" viewHeight="300"><nodes>
    <node name="Doc"><components>
      <component type="RmlUiDocumentComponent" space="SceneView"/>
    </components></node>
  </nodes></scene>
  <scene type="Scene" name="Scaled"><nodes>
    <node name="Doc"><components>
      <component type="RmlUiDocumentComponent" space="Screen" scaleMode="ScaleWithScreen" referenceWidth="1280" referenceHeight="720" match="0.5"/>
    </components></node>
  </nodes></scene>
</scenes></SceneManager>)";

        // The body fills the context (it has no size of its own in RmlUi, so a percentage in a screen would be of nothing) and lets clicks through (pointer-events),
        // like the groups of the other front-ends: only what has a box of its own takes them
        constexpr const char* kStyle = R"(
body { width: 100%; height: 100%; font-family: "PT Sans"; font-size: 20dp; color: white; pointer-events: none; }
)";

        eastl::string Document(const char* body, const char* style = "") {
            return Format("<rml><head><style>{}{}</style></head><body>{}</body></rml>", kStyle, style, body);
        }

        SDL_MouseButtonEvent Button(float x, float y, bool down) {
            SDL_MouseButtonEvent event{};
            event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            event.button = SDL_BUTTON_LEFT;
            event.down = down;
            event.x = x;
            event.y = y;
            return event;
        }

        SDL_MouseMotionEvent Motion(float x, float y, SDL_MouseButtonFlags buttonsHeld = 0) {
            SDL_MouseMotionEvent event{};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.x = x;
            event.y = y;
            event.state = buttonsHeld;
            return event;
        }

        /// Records the actions that documents emit and the mouse events that reach game code
        class Recorder : public SubscriptionHolder {
        public:
            Recorder() {
                Subscribe<InputEvents::ActionEvent, &Recorder::OnAction>(this);
                Subscribe<SDLEvents::MouseButtonDownEvent, &Recorder::OnDown>(this);
            }

            eastl::vector<PoolString> actions;
            int gameDowns = 0;

        private:
            void OnAction(const InputEvents::ActionEvent& event) {
                actions.push_back(event.action);
            }
            void OnDown(const SDLEvents::MouseButtonDownEvent&) {
                ++gameDowns;
            }
        };

    }  // namespace

    class RmlUiHostTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _savedSettings = CoreManager::GetUIEnvironment().GetSettings();
            _renderer = New<RecordingRenderer>();
            _renderer->width = 800.0f;
            _renderer->height = 600.0f;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(kScenesXml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
        }

        void TearDown() override {
            _manager.Reset();
            _fileManager.Reset();
            CoreManager::GetUIEnvironment().SetSettings(_savedSettings);
            CoreManager::GetTime().Reset();
            if (!_directory.empty()) {
                std::filesystem::remove_all(_directory);
            }
            CoreManager::GetUIVars().Clear();
            CoreManager::GetRendererManager().SetRenderer({});
            EXPECT_EQ(RmlUiRuntime::GetInstance().GetHolderCount(), 0);
        }

        Scene& GetScene(const char* name) const {
            return *_manager->GetSceneByName(PoolString::Intern(name));
        }

        IntrusivePtr<RmlUiDocumentComponent> Doc(const char* sceneName, const char* nodeName = "Doc") const {
            for (const auto& node : GetScene(sceneName).GetRootNode().GetChildren()) {
                if (node->GetName() == PoolString::Intern(nodeName)) {
                    return node->GetComponent<RmlUiDocumentComponent>();
                }
            }
            return {};
        }

        void Draw(const char* sceneName) {
            _manager->SetActiveScene(PoolString::Intern(sceneName));
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// The border box of an element, in output pixels
        static Rect BoxOf(const RmlUiDocumentComponent& doc, const char* id) {
            Rml::Element* element = doc.GetDocument() ? doc.GetDocument()->GetElementById(id) : nullptr;
            if (!element) {
                ADD_FAILURE() << "no element '" << id << "'";
                return {};
            }
            const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
            return {doc.GetOriginX() + offset.x, doc.GetOriginY() + offset.y, size.x, size.y};
        }

        static size_t CountGeometry(const RecordingRenderer& renderer) {
            return static_cast<size_t>(std::count_if(renderer.calls.begin(), renderer.calls.end(), [](const DrawCall& call) { return call.kind == DrawKind::Geometry; }));
        }

        /// A directory of its own, named after the running test (the tests of one executable run in parallel processes)
        std::filesystem::path MakeDirectory() {
            _directory = std::filesystem::temp_directory_path() / Format("BagiEngineRmlUiHostTest_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()).c_str();
            std::filesystem::remove_all(_directory);
            std::filesystem::create_directories(_directory);
            return _directory;
        }

        /// Writes a file and moves its time two seconds on, so an edit is seen whatever the resolution of the file system's clock
        static void Write(const std::filesystem::path& path, const char* text) {
            {
                std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
            }
            std::error_code error;
            std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(++_edits * 2), error);
        }

        /// A scene with one RmlUiDocumentComponent whose `source` is @p source (a real path), drawn in the output of the fixture's renderer
        IntrusivePtr<RmlUiDocumentComponent> OpenFile(const std::filesystem::path& source) {
            const eastl::string scene = Format(R"(<SceneManager><scenes><scene type="Scene" name="File"><nodes><node name="Doc"><components>
  <component type="RmlUiDocumentComponent" space="Screen" scaleMode="ConstantPixelSize" source="{}"/>
</components></node></nodes></scene></scenes></SceneManager>)",
                                               source.generic_string());
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(scene));
            _fileManager = New<SceneManager>();
            _fileManager->Deserialize(deserializer);
            _fileManager->Initialize();
            _fileManager->SetActiveScene("File"_intern);
            return _fileManager->GetSceneByName("File"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
        }

        void DrawFile() {
            _renderer->ClearCalls();
            _fileManager->DrawAll();
        }

        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        IntrusivePtrAtomic<SceneManager> _fileManager;
        UISettings _savedSettings;
        std::filesystem::path _directory;
        static inline int _edits = 0;
    };

    TEST_F(RmlUiHostTest, TheFieldsComeFromTheSceneConfig) {
        auto menu = Doc("Main", "Menu");
        auto fit = Doc("Fit");

        ASSERT_TRUE(menu && fit);
        EXPECT_TRUE(menu->_modal);
        EXPECT_FALSE(menu->_visible);
        EXPECT_EQ(menu->_scaleMode, CanvasScaleMode::ConstantPixelSize);
        EXPECT_EQ(fit->_space, CanvasSpace::SceneView);
        EXPECT_TRUE(RmlUiRuntime::GetInstance().IsUp());
        EXPECT_NE(menu->GetContext(), nullptr);
    }

    TEST_F(RmlUiHostTest, ABoxIsDrawnAsGeometryInOutputPixels) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="box"></div>)", "#box { position: absolute; left: 20dp; top: 30dp; width: 40dp; height: 50dp; background-color: #ff0000; }")));

        Draw("Main");

        ASSERT_GE(CountGeometry(*_renderer), 1u);
        const DrawCall* box = nullptr;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Geometry && call.texture == nullptr) {
                box = &call;
                break;
            }
        }
        ASSERT_NE(box, nullptr);
        EXPECT_NEAR(box->rect.x, 20.0f, kTolerance);
        EXPECT_NEAR(box->rect.y, 30.0f, kTolerance);
        EXPECT_NEAR(box->rect.w, 40.0f, kTolerance);
        EXPECT_NEAR(box->rect.h, 50.0f, kTolerance);
        EXPECT_EQ(box->color.r, 255);  // straight alpha: red stays red
        EXPECT_EQ(box->color.g, 0);
        EXPECT_EQ(box->color.a, 255);
    }

    TEST_F(RmlUiHostTest, ATranslucentBackgroundBecomesStraightAlpha) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="box"></div>)", "#box { position: absolute; width: 40dp; height: 50dp; background-color: rgba(0, 200, 100, 128); }")));

        Draw("Main");

        const DrawCall* box = nullptr;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind == DrawKind::Geometry && call.texture == nullptr) {
                box = &call;
            }
        }
        ASSERT_NE(box, nullptr);
        // RmlUi hands over premultiplied colors (0, 100, 50, 128); the host undoes it
        EXPECT_NEAR(box->color.g, 200, 2);
        EXPECT_NEAR(box->color.b, 100, 2);
        EXPECT_EQ(box->color.a, 128);
    }

    TEST_F(RmlUiHostTest, TextIsDrawnFromAGlyphTexture) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<p id="t">Hello, Привет</p>)")));

        Draw("Main");

        bool textured = false;
        for (const DrawCall& call : _renderer->calls) {
            textured |= call.kind == DrawKind::Geometry && call.texture != nullptr;
        }
        EXPECT_TRUE(textured);
        EXPECT_GE(_renderer->texturesCreated, 1u);
        EXPECT_GT(BoxOf(*doc, "t").w, 0.0f);
    }

    // The glyphs of the text sit on pages of the font engine; each quad is four vertices and six indices, with its UV inside the page
    TEST_F(RmlUiHostTest, TextIsMadeOfGlyphQuadsInsideThePage) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<p id="t">Hello, Привет</p>)")));

        Draw("Main");

        size_t quads = 0;
        for (const DrawCall& call : _renderer->calls) {
            if (call.kind != DrawKind::Geometry || call.texture == nullptr) {
                continue;
            }
            EXPECT_EQ(call.vertices.size() % 4, 0u);
            EXPECT_EQ(call.indices.size(), call.vertices.size() / 4 * 6);
            quads += call.vertices.size() / 4;
            for (const GeometryVertex& vertex : call.vertices) {
                EXPECT_GE(vertex.u, 0.0f);
                EXPECT_LE(vertex.u, 1.0f);
                EXPECT_GE(vertex.v, 0.0f);
                EXPECT_LE(vertex.v, 1.0f);
            }
        }
        EXPECT_EQ(quads, 12u);  // "Hello, Привет" without the space: 11 letters and a comma
    }

    // A texture made from a page is never rewritten: a glyph that comes after the first draw goes to a new page
    TEST_F(RmlUiHostTest, AGlyphThatComesAfterTheFirstDrawGoesToANewPage) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<p>Hello</p>)")));
        Draw("Main");
        const size_t pagesBefore = RmlUiRuntime::GetInstance().GetFontEngine().GetPageCount();
        ASSERT_GE(pagesBefore, 1u);

        ASSERT_TRUE(doc->SetMarkup(Document(R"(<p>Привет</p>)")));
        Draw("Main");

        EXPECT_EQ(RmlUiRuntime::GetInstance().GetFontEngine().GetPageCount(), pagesBefore + 1);
    }

    TEST_F(RmlUiHostTest, TheContextFollowsTheScaleOfTheScaler) {
        auto doc = Doc("Scaled");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="box"></div>)", "#box { position: absolute; left: 100dp; top: 50dp; width: 200dp; height: 40dp; }")));

        _renderer->width = 1280.0f;
        _renderer->height = 720.0f;
        Draw("Scaled");
        const Rect atReference = BoxOf(*doc, "box");
        _renderer->width = 2560.0f;
        _renderer->height = 1440.0f;
        Draw("Scaled");
        const Rect atDouble = BoxOf(*doc, "box");

        EXPECT_NEAR(atReference.x, 100.0f, kTolerance);
        EXPECT_NEAR(atReference.w, 200.0f, kTolerance);
        EXPECT_NEAR(atDouble.x, 200.0f, kTolerance);
        EXPECT_NEAR(atDouble.w, 400.0f, kTolerance);
        EXPECT_NEAR(atDouble.h, 80.0f, kTolerance);
        EXPECT_EQ(doc->GetContextWidth(), 2560);
    }

    TEST_F(RmlUiHostTest, ASceneViewDocumentFillsTheFittedView) {
        auto doc = Doc("Fit");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="box"></div>)", "#box { position: absolute; left: 0dp; top: 0dp; width: 100%; height: 100%; }")));

        _renderer->width = 800.0f;  // the 400 x 300 view fits at scale 2 in 800 x 600
        _renderer->height = 600.0f;
        Draw("Fit");
        EXPECT_NEAR(BoxOf(*doc, "box").w, 800.0f, kTolerance);

        _renderer->width = 1200.0f;  // wider output: bars at the sides, the view is 800 x 600 at x = 200
        Draw("Fit");
        const Rect box = BoxOf(*doc, "box");
        EXPECT_NEAR(box.x, 200.0f, kTolerance);
        EXPECT_NEAR(box.w, 800.0f, kTolerance);
        EXPECT_NEAR(box.h, 600.0f, kTolerance);
    }

    TEST_F(RmlUiHostTest, AClipOfTheDocumentIsBalancedAfterTheDraw) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="scroll"><p>one</p><p>two</p></div>)", "#scroll { width: 100dp; height: 20dp; overflow: hidden; background-color: #333; }")));

        Draw("Main");

        EXPECT_EQ(_renderer->GetClipDepth(), 0u);
        bool clipped = false;
        for (const DrawCall& call : _renderer->calls) {
            clipped |= call.clip.has_value();
        }
        EXPECT_TRUE(clipped) << "the text inside an overflow:hidden box is drawn under a clip";
    }

    TEST_F(RmlUiHostTest, AHiddenDocumentDrawsNothing) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div style="width: 10dp; height: 10dp; background-color: red;"></div>)")));
        doc->_visible = false;

        Draw("Main");

        EXPECT_EQ(CountGeometry(*_renderer), 0u);
    }

    // ---- input ---------------------------------------------------------------------------------------------------------------

    TEST_F(RmlUiHostTest, AClickOnAButtonEmitsItsAction) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<button id="play" data-action="Match3Play">Play</button>)",
                                            "#play { position: absolute; left: 100dp; top: 100dp; width: 120dp; height: 40dp; background-color: #285a3c; pointer-events: auto; }")));
        Draw("Main");
        Recorder recorder;
        UIInput& input = CoreManager::GetUIInput();

        const bool down = input.DispatchMouseButtonDown(Button(150.0f, 120.0f, true));
        const bool up = input.DispatchMouseButtonUp(Button(150.0f, 120.0f, false));

        EXPECT_TRUE(down);
        EXPECT_TRUE(up);
        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "Match3Play"_intern);
        EXPECT_EQ(recorder.gameDowns, 0) << "the click did not reach the game";
    }

    TEST_F(RmlUiHostTest, AReleaseElsewhereCancelsTheClick) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<button id="play" data-action="Match3Play">Play</button>)",
                                            "#play { position: absolute; left: 100dp; top: 100dp; width: 120dp; height: 40dp; background-color: #285a3c; pointer-events: auto; }")));
        Draw("Main");
        Recorder recorder;
        UIInput& input = CoreManager::GetUIInput();

        input.DispatchMouseButtonDown(Button(150.0f, 120.0f, true));
        const bool up = input.DispatchMouseButtonUp(Button(600.0f, 500.0f, false));

        EXPECT_TRUE(up) << "the release of a press the document took is the document's";
        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(RmlUiHostTest, AClickOutsideTheElementsGoesToTheGame) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<button id="play" data-action="Match3Play">Play</button>)",
                                            "#play { position: absolute; left: 100dp; top: 100dp; width: 120dp; height: 40dp; background-color: #285a3c; pointer-events: auto; }")));
        Draw("Main");
        Recorder recorder;

        const bool down = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(600.0f, 500.0f, true));

        EXPECT_FALSE(down) << "pointer-events: none on the body lets the click through";
        EXPECT_EQ(recorder.gameDowns, 1);
    }

    TEST_F(RmlUiHostTest, ADisabledButtonEmitsNothing) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<button id="play" data-action="Match3Play" disabled>Play</button>)",
                                            "#play { position: absolute; left: 100dp; top: 100dp; width: 120dp; height: 40dp; background-color: #285a3c; pointer-events: auto; }")));
        Draw("Main");
        Recorder recorder;
        UIInput& input = CoreManager::GetUIInput();

        input.DispatchMouseButtonDown(Button(150.0f, 120.0f, true));
        input.DispatchMouseButtonUp(Button(150.0f, 120.0f, false));

        EXPECT_TRUE(recorder.actions.empty());
    }

    TEST_F(RmlUiHostTest, TheButtonLooksHoveredUnderThePointer) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<button id="play" data-action="Go">Play</button>)",
                                            "#play { position: absolute; left: 100dp; top: 100dp; width: 120dp; height: 40dp; background-color: #112233; pointer-events: auto; }\n"
                                            "#play:hover { background-color: #aabbcc; }")));
        Draw("Main");
        const auto colorOfButton = [this]() {
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Geometry && call.texture == nullptr) {
                    return call.color;
                }
            }
            return Color{};
        };
        EXPECT_EQ(colorOfButton().r, 0x11);

        CoreManager::GetUIInput().DispatchMouseMotion(Motion(150.0f, 120.0f));
        Draw("Main");

        EXPECT_EQ(colorOfButton().r, 0xAA);
    }

    TEST_F(RmlUiHostTest, AModalDocumentTakesEveryClick) {
        auto menu = Doc("Main", "Menu");
        ASSERT_TRUE(menu->SetMarkup(Document(R"(<div id="plate"></div>)", "#plate { width: 10dp; height: 10dp; background-color: #333; }")));
        menu->_visible = true;
        Draw("Main");
        Recorder recorder;

        const bool down = CoreManager::GetUIInput().DispatchMouseButtonDown(Button(700.0f, 500.0f, true));

        EXPECT_TRUE(down);
        EXPECT_EQ(recorder.gameDowns, 0);
    }

    // ---- UIVars --------------------------------------------------------------------------------------------------------------

    TEST_F(RmlUiHostTest, ALabelFollowsTheVariablesOfTheGame) {
        auto doc = Doc("Main");
        CoreManager::GetUIVars().Set("score"_intern, 120);
        ASSERT_TRUE(doc->SetMarkup(R"(<rml><head><style>body { font-family: "PT Sans"; }</style></head><body data-model="vars"><p id="score">Score: {{vars.score}}</p></body></rml>)"));

        Draw("Main");
        EXPECT_EQ(doc->GetDocument()->GetElementById("score")->GetInnerRML(), "Score: 120");

        CoreManager::GetUIVars().Set("score"_intern, 250);
        Draw("Main");
        EXPECT_EQ(doc->GetDocument()->GetElementById("score")->GetInnerRML(), "Score: 250");
    }

    // ---- a document in a file ---------------------------------------------------------------------------------------------------

    TEST_F(RmlUiHostTest, ADocumentIsLoadedFromAFileWithItsStylesheet) {
        const std::filesystem::path directory = std::filesystem::temp_directory_path() / Format("BagiEngineRmlUiHostTest_{}", ::testing::UnitTest::GetInstance()->current_test_info()->name()).c_str();
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "menu.rml") << R"(<rml><head><link type="text/rcss" href="menu.rcss"/></head><body><div id="box"></div></body></rml>)";
        std::ofstream(directory / "menu.rcss") << "body { width: 100%; height: 100%; } #box { position: absolute; left: 12dp; top: 34dp; width: 56dp; height: 78dp; background-color: #123456; }";

        const eastl::string scene = Format(R"(<SceneManager><scenes><scene type="Scene" name="File"><nodes><node name="Doc"><components>
  <component type="RmlUiDocumentComponent" space="Screen" scaleMode="ConstantPixelSize" source="{}"/>
</components></node></nodes></scene></scenes></SceneManager>)",
                                           (directory / "menu.rml").generic_string());
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(scene));
        auto manager = New<SceneManager>();
        manager->Deserialize(deserializer);
        manager->Initialize();
        manager->SetActiveScene("File"_intern);
        auto doc = manager->GetSceneByName("File"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
        ASSERT_TRUE(doc);
        ASSERT_NE(doc->GetDocument(), nullptr) << "the file was read through the file interface";

        manager->DrawAll();

        const Rect box = BoxOf(*doc, "box");
        EXPECT_NEAR(box.x, 12.0f, kTolerance) << "the stylesheet next to the document was found";
        EXPECT_NEAR(box.w, 56.0f, kTolerance);
        EXPECT_NEAR(box.h, 78.0f, kTolerance);
        doc.Reset();
        manager.Reset();
        std::filesystem::remove_all(directory);
    }

    // ---- hot reload ---------------------------------------------------------------------------------------------------------------

    namespace {

        constexpr const char* kBoxDocument = R"(<rml><head><link type="text/rcss" href="menu.rcss"/></head><body><div id="box"></div></body></rml>)";

        eastl::string BoxStyle(int left) {
            return Format("body {{ width: 100%; height: 100%; }} #box {{ position: absolute; left: {}dp; top: 34dp; width: 56dp; height: 78dp; background-color: #123456; }}", left);
        }

    }  // namespace

    TEST_F(RmlUiHostTest, AChangedStylesheetLoadsTheDocumentAgain) {
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        ASSERT_NE(doc->GetDocument(), nullptr);
        DrawFile();
        ASSERT_NEAR(BoxOf(*doc, "box").x, 12.0f, kTolerance);

        Write(directory / "menu.rcss", BoxStyle(80).c_str());
        EXPECT_TRUE(doc->ReloadIfChanged());

        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 80.0f, kTolerance) << "the style sheet was read again, not taken from the cache";
        EXPECT_FALSE(doc->ReloadIfChanged()) << "the new files are the ones watched now";
    }

    TEST_F(RmlUiHostTest, AChangedDocumentFileLoadsTheDocumentAgain) {
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        DrawFile();
        ASSERT_EQ(doc->GetDocument()->GetElementById("extra"), nullptr);

        Write(directory / "menu.rml", R"(<rml><head><link type="text/rcss" href="menu.rcss"/></head><body><div id="box"></div><div id="extra"></div></body></rml>)");

        EXPECT_TRUE(doc->ReloadIfChanged());
        EXPECT_NE(doc->GetDocument()->GetElementById("extra"), nullptr);
    }

    TEST_F(RmlUiHostTest, ADocumentThatHasNotChangedIsLeftAlone) {
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        Rml::ElementDocument* before = doc->GetDocument();

        EXPECT_FALSE(doc->ReloadIfChanged());

        EXPECT_EQ(doc->GetDocument(), before);
    }

    // RmlUi's parser takes nearly any text for a document, so a load fails only when the file cannot be read
    TEST_F(RmlUiHostTest, ADocumentThatCannotBeReadKeepsTheOldOneOnScreen) {
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        DrawFile();
        Rml::ElementDocument* before = doc->GetDocument();

        std::filesystem::remove(directory / "menu.rml");
        EXPECT_FALSE(doc->ReloadIfChanged()) << "a file that is gone (an editor saves by deleting and renaming) is not a change yet";
        EXPECT_FALSE(doc->Reload());

        EXPECT_EQ(doc->GetDocument(), before) << "the screen is not blanked";
        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 12.0f, kTolerance);

        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(40).c_str());
        EXPECT_TRUE(doc->ReloadIfChanged()) << "the file is back, and the load is tried again";
        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 40.0f, kTolerance);
    }

    TEST_F(RmlUiHostTest, AFileThatIsWrittenLaterIsNoticed) {
        const auto directory = MakeDirectory();
        auto doc = OpenFile(directory / "late.rml");
        ASSERT_EQ(doc->GetDocument(), nullptr);

        Write(directory / "menu.rcss", BoxStyle(25).c_str());
        Write(directory / "late.rml", R"(<rml><head><link type="text/rcss" href="menu.rcss"/></head><body><div id="box"></div></body></rml>)");

        EXPECT_TRUE(doc->ReloadIfChanged());
        ASSERT_NE(doc->GetDocument(), nullptr);
        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 25.0f, kTolerance);
    }

    TEST_F(RmlUiHostTest, TheDrawLooksAtTheFilesOncePerInterval) {
        UISettings settings = _savedSettings;
        settings.hotReloadInterval = 0.5f;
        CoreManager::GetUIEnvironment().SetSettings(settings);
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        DrawFile();  // the first draw looks, and finds nothing

        Write(directory / "menu.rcss", BoxStyle(80).c_str());
        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 12.0f, kTolerance) << "less than an interval has passed";

        // The clock clamps a frame to Time::kMaxFrameTime, so three frames make more than the interval
        for (int frame = 0; frame < 3; ++frame) {
            CoreManager::GetTime().Advance(Time::kMaxFrameTime);
        }
        DrawFile();
        EXPECT_NEAR(BoxOf(*doc, "box").x, 80.0f, kTolerance);
    }

    TEST_F(RmlUiHostTest, AnIntervalOfZeroTurnsHotReloadOff) {
        UISettings settings = _savedSettings;
        settings.hotReloadInterval = 0.0f;
        CoreManager::GetUIEnvironment().SetSettings(settings);
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        DrawFile();

        Write(directory / "menu.rcss", BoxStyle(80).c_str());
        for (int frame = 0; frame < 8; ++frame) {
            CoreManager::GetTime().Advance(Time::kMaxFrameTime);
        }
        DrawFile();

        EXPECT_NEAR(BoxOf(*doc, "box").x, 12.0f, kTolerance);
        EXPECT_TRUE(doc->ReloadIfChanged()) << "a call still works";
    }

    TEST_F(RmlUiHostTest, AReloadKeepsTheVariablesAndTheActions) {
        const auto directory = MakeDirectory();
        constexpr const char* markup = R"(<rml><head><style>body {{ width: 100%; height: 100%; font-family: "PT Sans"; font-size: 20dp; color: white; pointer-events: none; }}
#play {{ position: absolute; left: {}dp; top: 100dp; width: 120dp; height: 40dp; background-color: #285a3c; pointer-events: auto; }}</style></head>
<body data-model="vars"><button id="play" data-action="Match3Play">Score {{{{vars.score}}}}</button></body></rml>)";
        Write(directory / "menu.rml", Format(markup, 100).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        CoreManager::GetUIVars().Set("score"_intern, 7);
        DrawFile();
        ASSERT_EQ(doc->GetDocument()->GetElementById("play")->GetInnerRML(), "Score 7");

        Write(directory / "menu.rml", Format(markup, 300).c_str());
        ASSERT_TRUE(doc->ReloadIfChanged());
        CoreManager::GetUIVars().Set("score"_intern, 8);
        DrawFile();

        EXPECT_EQ(doc->GetDocument()->GetElementById("play")->GetInnerRML(), "Score 8") << "the model of the context follows the new document";
        Recorder recorder;
        UIInput& input = CoreManager::GetUIInput();
        EXPECT_FALSE(input.DispatchMouseButtonDown(Button(150.0f, 120.0f, true))) << "the old place of the button is empty";
        input.DispatchMouseButtonUp(Button(150.0f, 120.0f, false));
        EXPECT_TRUE(recorder.actions.empty());
        EXPECT_TRUE(input.DispatchMouseButtonDown(Button(350.0f, 120.0f, true)));
        input.DispatchMouseButtonUp(Button(350.0f, 120.0f, false));
        ASSERT_EQ(recorder.actions.size(), 1u);
        EXPECT_EQ(recorder.actions[0], "Match3Play"_intern);
    }

    TEST_F(RmlUiHostTest, MarkupFromCodeIsNotReplacedByTheFiles) {
        const auto directory = MakeDirectory();
        Write(directory / "menu.rml", kBoxDocument);
        Write(directory / "menu.rcss", BoxStyle(12).c_str());
        auto doc = OpenFile(directory / "menu.rml");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="other"></div>)")));

        Write(directory / "menu.rcss", BoxStyle(80).c_str());

        EXPECT_FALSE(doc->ReloadIfChanged());
        EXPECT_NE(doc->GetDocument()->GetElementById("other"), nullptr);
    }

    TEST_F(RmlUiHostTest, AComponentWithoutASourceHasNothingToReload) {
        auto doc = Doc("Main");
        ASSERT_TRUE(doc->SetMarkup(Document(R"(<div id="box"></div>)")));

        EXPECT_FALSE(doc->Reload());
        EXPECT_FALSE(doc->ReloadIfChanged());
        EXPECT_NE(doc->GetDocument()->GetElementById("box"), nullptr);
    }

    TEST_F(RmlUiHostTest, AMissingFileLeavesTheComponentWithoutADocument) {
        const eastl::string scene = R"(<SceneManager><scenes><scene type="Scene" name="Missing"><nodes><node name="Doc"><components>
  <component type="RmlUiDocumentComponent" source="assets/ui/does-not-exist.rml"/>
</components></node></nodes></scene></scenes></SceneManager>)";
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(scene));
        auto manager = New<SceneManager>();
        manager->Deserialize(deserializer);
        manager->Initialize();
        manager->SetActiveScene("Missing"_intern);
        auto doc = manager->GetSceneByName("Missing"_intern)->GetRootNode().GetChildren()[0]->GetComponent<RmlUiDocumentComponent>();
        ASSERT_TRUE(doc);

        EXPECT_EQ(doc->GetDocument(), nullptr);
        manager->DrawAll();  // draws nothing and does not crash
        EXPECT_EQ(CountGeometry(*_renderer), 0u);
        doc.Reset();
        manager.Reset();
    }

    TEST_F(RmlUiHostTest, TheContextOfAComponentIsGoneWithIt) {
        {
            auto doc = Doc("Main");
            ASSERT_TRUE(doc->SetMarkup(Document("<p>x</p>")));
            EXPECT_EQ(RmlUiRuntime::GetInstance().GetHolderCount(), 4) << "each of the four components of the scenes holds the runtime";
        }
        _manager.Reset();

        EXPECT_EQ(RmlUiRuntime::GetInstance().GetHolderCount(), 0);
        EXPECT_FALSE(RmlUiRuntime::GetInstance().IsUp());
        EXPECT_EQ(RmlUiRuntime::GetInstance().GetRenderInterface().GetTextureCount(), 0u);
        EXPECT_EQ(RmlUiRuntime::GetInstance().GetRenderInterface().GetGeometryCount(), 0u);
    }

}  // namespace BECore::Tests
