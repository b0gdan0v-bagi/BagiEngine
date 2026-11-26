// The Scene Editor in remote mode: the widget and its two shipped bars over the fake program of FakeControlEndpoint, beside a scene of this process
// that must stay as it is, drawn in an ImGui context with no backend.

#include <BECore/Config/ConfigManager.h>
#include <BECore/Config/ConfigNames.h>
#include <BECore/FakeControlEndpoint.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <Widgets/RemoteEditor/RemoteSessionRegistry.h>
#include <Widgets/SceneEditor/SceneDialogEvents.h>
#include <Widgets/SceneEditor/SceneEditorWidget.h>
#include <Widgets/SceneEditor/SceneSelectionEvents.h>
#include <Widgets/Viewport/ViewportWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 6;
        constexpr int kSteadyFrames = 10;
        /// Long enough per frame for the session's reads to fall due within a few frames, short of its first retry
        constexpr std::chrono::milliseconds kFrameStep{100};
        constexpr StringView kToolbarAsset = "SceneEditorToolbarPanel";
        constexpr StringView kInspectorAsset = "SceneInspectorPanel";
        constexpr StringView kAddress = "ws://127.0.0.1:8765";
        constexpr int32_t kLayer = 1;
        /// More rows than the hierarchy shows at once
        constexpr int kRowsAboveCell = 40;
        /// A fraction of the program's 800 by 600 screen inside "Cell"
        constexpr double kOnCell = 0.05;

        /// The same nodes in both scenes, so that an edit that goes to the wrong one is seen. "Cell" is the only one a pick hits
        String Nodes() {
            String nodes(R"(<node name="Board" layer="1"><components><component type="TransformComponent" x="10"/><component type="TextComponent" text="Hi"/></components><children>)");
            for (int i = 0; i < kRowsAboveCell; ++i) {
                nodes += Format(R"(<node name="Filler{}"/>)", i);
            }
            nodes += R"(<node name="Cell"><components><component type="TransformComponent" width="80" height="60"/><component type="QuadRendererComponent"/></components></node></children></node>)";
            return nodes;
        }

        /// What the other panels are told and asked for
        class SceneEventRecorder : public SubscriptionHolder {
        public:
            SceneEventRecorder() {
                Subscribe<SceneSelectionChangedEvent, &SceneEventRecorder::OnSelection>(this);
                Subscribe<SceneAddComponentRequestEvent, &SceneEventRecorder::OnAddComponent>(this);
            }

            size_t selected = 0;
            size_t addComponentRequests = 0;

        private:
            void OnSelection(const SceneSelectionChangedEvent& event) {
                selected = event.nodes.size();
            }

            void OnAddComponent(const SceneAddComponentRequestEvent& /*event*/) {
                ++addComponentRequests;
            }
        };

    }  // namespace

    class RemoteSceneEditorTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _endpoint = MakeUnique<FakeControlEndpoint>(Nodes());
            _session = MakeUnique<RemoteSession>(_endpoint->Factory());
            _sessionHandle = RemoteSessionRegistry::GetInstance().Register(*_session);

            // The scene of this process and the file Save writes it into
            _dir = TestTempDirectory("remote_scene_editor");
            MakeEmptyDirectory(_dir);
            const Path panels = Path(__FILE__).Parent().Parent().Parent() / "config" / "panels";
            for (const StringView asset : {kToolbarAsset, kInspectorAsset}) {
                WriteFile(_dir / Format("{}.xml", asset), ReadFile(panels / Format("{}.xml", asset)));
            }
            const String sceneText = SceneConfigText(Nodes());
            _sceneFile = _dir / "SceneConfig.xml";
            WriteFile(_sceneFile, sceneText);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _config.Initialize();
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(sceneText.c_str()));
            _local = New<SceneManager>();
            _local->Deserialize(deserializer);
            _local->Initialize();
            EngineEnvironment::UseSceneManager(_local);

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());

            ASSERT_NO_FATAL_FAILURE(StartBar(_toolbar, kToolbarAsset));
            ASSERT_NO_FATAL_FAILURE(StartBar(_inspector, kInspectorAsset));
            _editor = New<SceneEditorWidget>();
            _editor->SetConfigManager(_config);
            XmlDeserializer widgetConfig;
            ASSERT_TRUE(_editor->Initialize(widgetConfig));
            Settle();
        }

        void TearDown() override {
            _editor.Reset();
            _toolbar.Reset();
            _inspector.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            EngineEnvironment::UseSceneManager({});
            _local.Reset();
            _sessionHandle.Reset();
            _session.reset();
            _endpoint.reset();
            RemoveDirectory(_dir);
        }

        void StartBar(IntrusivePtr<AssetPanelWidget>& widget, StringView asset) {
            widget = New<AssetPanelWidget>();
            widget->SetConfigManager(_config);
            widget->SetAsset(asset);
            const UniquePtr<IDeserializer> deserializer = _config.CreateDeserializer(PoolString::Intern(asset));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(widget->Initialize(*deserializer));
        }

        /// A frame of the editor: the link is polled and the program answers, then the bars update and the Scene Editor draws
        void Frame() {
            _session->Poll(_now += kFrameStep);
            _endpoint->Serve();
            _session->Poll(_now);
            DrawFrame();
        }

        /// The panels' part of a frame, with nothing from the link
        void DrawFrame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _toolbar->Update();
            _inspector->Update();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            _editor->Update();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        void Attach() {
            ASSERT_TRUE(_session->Attach(kAddress).has_value());
            Settle();
            ASSERT_EQ(_session->GetState(), RemoteSessionState::Attached);
        }

        void Run(PoolString command) {
            EditorCommandEvent::Emit(command);
            Settle();
        }

        /// The visible item whose label, or its part before `##`, is @p label
        const ImGuiItemRecord* Find(StringView label) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.MatchesLabel(label)) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The visible item whose label holds @p text
        const ImGuiItemRecord* FindText(StringView text) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label).find(text) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(const ImGuiItemRecord* item) {
            ASSERT_NE(item, nullptr);
            ClickPoint({item->CenterX(), item->CenterY()});
        }

        void ClickPoint(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        /// Opens the closed root row of the hierarchy by its arrow and clicks the row of the node below it
        void SelectInHierarchy(StringView name) {
            const ImGuiItemRecord* const root = FindText("Root");
            ASSERT_NE(root, nullptr);
            ClickPoint({root->minX + (root->maxY - root->minY) * 0.5f, root->CenterY()});
            ClickAt(FindText(name));
        }

        void Type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
        }

        void PressEnter() {
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
            Frame();
            ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
            Settle();
        }

        [[nodiscard]] Scene& LocalScene() {
            return Require(_local->GetActiveScene());
        }

        [[nodiscard]] SceneNode& LocalNode() {
            return Require(FindNodeBelow(LocalScene().GetRootNode(), "Board"));
        }

        /// The TextComponent of @p node as it would be saved
        [[nodiscard]] static String TextOf(const SceneNode& node) {
            return At(ComponentTexts(node), 1);
        }

        void SelectLocalNode() {
            SelectSceneNodeRequestEvent::Emit(&LocalScene(), &LocalNode());
            Settle();
        }

        UniquePtr<FakeControlEndpoint> _endpoint;
        UniquePtr<RemoteSession> _session;
        RemoteSessionHandle _sessionHandle;
        RemoteSessionTime _now{};
        Path _dir;
        Path _sceneFile;
        FileSystem _fileSystem;
        ConfigManager _config{_fileSystem};
        IntrusivePtr<SceneManager> _local;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _toolbar;
        IntrusivePtr<AssetPanelWidget> _inspector;
        IntrusivePtr<SceneEditorWidget> _editor;
        SceneEventRecorder _events;
    };

    TEST_F(RemoteSceneEditorTest, AttachedTheLocalSceneIsNeitherSelectedNorExtendedNorSavedAndDetachGivesItBack) {
        SelectLocalNode();
        SceneEditedEvent::Emit(&LocalScene());
        ASSERT_TRUE(_editor->GetInspected().hasSelection);
        ASSERT_TRUE(_editor->GetInspected().dirty);
        ASSERT_EQ(_events.selected, 1u);
        const String saved = ReadFile(_sceneFile);

        ASSERT_NO_FATAL_FAILURE(Attach());
        EXPECT_EQ(_events.selected, 0u) << "the other panels still hold a local node";
        EXPECT_FALSE(_editor->GetInspected().hasSelection);
        EXPECT_FALSE(_editor->GetInspected().dirty);

        SelectLocalNode();
        EXPECT_FALSE(_editor->GetInspected().hasSelection);
        Run(EditorCommandIds::kSceneEditorAddComponent);
        _editor->RenameInspected("Renamed");
        _editor->SetInspectedLayer(kLayer + 1);
        Run(EditorCommandIds::kSceneSave);
        EXPECT_EQ(_events.addComponentRequests, 0u);
        EXPECT_EQ(LocalNode().GetLayer(), kLayer);
        EXPECT_EQ(ReadFile(_sceneFile), saved);

        _session->Detach();
        Settle();
        EXPECT_TRUE(_editor->GetInspected().dirty);
        Run(EditorCommandIds::kSceneSave);
        EXPECT_NE(ReadFile(_sceneFile), saved);
        EXPECT_FALSE(_editor->GetInspected().dirty);
    }

    TEST_F(RemoteSceneEditorTest, ALayerStepAndATypedTextReachTheAttachedProgramAndNotTheLocalScene) {
        const String localText = TextOf(LocalNode());
        const String remoteText = TextOf(_endpoint->Node("Board"));
        ASSERT_NO_FATAL_FAILURE(Attach());
        ASSERT_NO_FATAL_FAILURE(SelectInHierarchy("Board"));
        ASSERT_TRUE(_editor->GetInspected().hasSelection);

        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("+")));
        EXPECT_EQ(_endpoint->Node("Board").GetLayer(), kLayer + 1);
        EXPECT_EQ(_editor->GetInspected().layer, kLayer + 1);

        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("TextComponent")));
        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("text")));
        Type("!");
        EXPECT_EQ(TextOf(_endpoint->Node("Board")), remoteText) << "a text that is still being typed was sent";
        PressEnter();
        EXPECT_NE(TextOf(_endpoint->Node("Board")).find("Hi!"), String::npos) << TextOf(_endpoint->Node("Board")).c_str();

        EXPECT_EQ(TextOf(LocalNode()), localText);
        EXPECT_EQ(LocalNode().GetLayer(), kLayer);
        EXPECT_FALSE(_editor->GetInspected().dirty);
    }

    TEST_F(RemoteSceneEditorTest, ALostLinkTakesNoEditAndKeepsWhatWasRead) {
        ASSERT_NO_FATAL_FAILURE(Attach());
        ASSERT_NO_FATAL_FAILURE(SelectInHierarchy("Board"));
        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("TextComponent")));
        const String remoteText = TextOf(_endpoint->Node("Board"));

        _endpoint->SetRefusing(true);
        _endpoint->Drop();
        Settle();
        ASSERT_EQ(_session->GetState(), RemoteSessionState::Lost);

        EXPECT_TRUE(_editor->GetInspected().hasSelection);
        EXPECT_EQ(_editor->GetInspected().name, StringView("Board"));
        EXPECT_TRUE(_editor->GetInspected().layerReadOnly);
        ASSERT_NE(Find("text"), nullptr) << "the fields that were read left the screen";

        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("+")));
        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("text")));
        Type("!");
        PressEnter();
        _editor->SetInspectedLayer(kLayer + 1);
        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
        EXPECT_EQ(_endpoint->Node("Board").GetLayer(), kLayer);
        EXPECT_EQ(TextOf(_endpoint->Node("Board")), remoteText);
    }

    TEST_F(RemoteSceneEditorTest, ASteadyAttachedFrameAllocatesNothing) {
        const IntrusivePtr<ViewportWidget> viewport = New<ViewportWidget>();
        XmlDeserializer widgetConfig;
        ASSERT_TRUE(viewport->Initialize(widgetConfig));
        ASSERT_NO_FATAL_FAILURE(Attach());
        ASSERT_NO_FATAL_FAILURE(SelectInHierarchy("Board"));
        ASSERT_NO_FATAL_FAILURE(ClickAt(Find("TextComponent")));
        // The index keeps a record per item and is not what is measured
        _index.Disarm();
        const auto frame = [this, &viewport] {
            DrawFrame();
            ImGui::NewFrame();
            viewport->Update();
            ImGui::Render();
        };
        for (int i = 0; i < kSettleFrames; ++i) {
            frame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                frame();
            }
        });
    }

    TEST_F(RemoteSceneEditorTest, APickedNodeOpensTheClosedPanelAndItsRowIsBroughtIntoSightPastAFilter) {
        ASSERT_NO_FATAL_FAILURE(Attach());
        _endpoint->DrawScreen();
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindText("##search")));
        Type("Filler");
        Run(EditorCommandIds::kViewSceneEditor);
        ASSERT_EQ(FindText("Root"), nullptr) << "the panel did not close";

        _session->RequestPick(kOnCell, kOnCell);
        Settle();

        ASSERT_EQ(_session->GetSelection(), Optional<String>("Board/Cell"));
        EXPECT_NE(FindText("Cell"), nullptr) << "the row of the picked node is not on the screen";
        EXPECT_EQ(_editor->GetInspected().name, StringView("Cell"));
        EXPECT_EQ(_events.selected, 0u) << "a node of this process was selected";

        // One walk scrolls to the row, so the user can scroll away from it
        const ImGuiItemRecord* const row = FindText("Cell");
        ASSERT_NE(row, nullptr);
        ImGui::GetIO().AddMousePosEvent(row->CenterX(), row->CenterY());
        Frame();
        ImGui::GetIO().AddMouseWheelEvent(0.0f, 100.0f);
        Settle();
        EXPECT_NE(FindText("Root"), nullptr);
        EXPECT_EQ(FindText("Cell"), nullptr);
    }

    TEST_F(RemoteSceneEditorTest, ARefusedWriteIsGivenToTheInspector) {
        ASSERT_NO_FATAL_FAILURE(Attach());
        ASSERT_TRUE(_editor->GetInspected().refusalCode.empty());

        _session->GetWrites().Edit({"Board", "TextComponent", 0, "no_such_field"}, PropertyValue{true}, RemoteEditKind::Value);
        Settle();

        EXPECT_EQ(_editor->GetInspected().refusalCode, StringView("not_found"));
        EXPECT_FALSE(_editor->GetInspected().refusalMessage.empty());
    }

}  // namespace BECore::Tests
