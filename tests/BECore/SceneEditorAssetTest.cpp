#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <BECore/SceneEditorHostProbe.h>
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
#include <Widgets/PanelAssets/PanelBars.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 10;
        constexpr StringView kToolbarAsset = "SceneEditorToolbarPanel";
        constexpr StringView kInspectorAsset = "SceneInspectorPanel";
        constexpr StringView kComponentsSlot = "sceneComponents";
        constexpr const char* kWindowName = "Scene Editor";
        constexpr const char* kInspectorChild = "##inspector";
        constexpr StringView kNodeName = "Hero";
        constexpr StringView kTyped = "X";
        constexpr int32_t kLayer = 4;
        constexpr float kWidthTolerance = 1.0f;
        /// A button with a text is labelled `<icon> <text>`, so it is found by its visible text, as the QA moves find Save
        constexpr StringView kSaveText = "Save";
        constexpr StringView kAddComponentText = "Add Component";

        /// The commands a panel's subscriber would see
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            [[nodiscard]] size_t Count(PoolString id) const {
                size_t count = 0;
                for (const PoolString seen : ids) {
                    count += seen == id ? 1 : 0;
                }
                return count;
            }

            Vector<PoolString> ids;

        private:
            void OnCommand(const EditorCommandEvent& event) {
                ids.push_back(event.commandId);
            }
        };

        Path ShippedFile(StringView asset) {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / Format("{}.xml", asset);
        }

        bool EndsWith(StringView text, StringView suffix) {
            return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
        }

    }  // namespace

    /// The two shipped bars of the Scene Editor hosted by real `AssetPanelWidget`s over their real models and a fake `ISceneEditorHost`, drawn the way
    /// `SceneEditorWidget` draws them (the toolbar at the top of its window, the inspector inside its `##inspector` child) in an ImGui context with no
    /// backend; the components slot is the test's own counter
    class SceneEditorAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("scene_editor_bars");
            MakeEmptyDirectory(_dir);
            for (const StringView asset : {kToolbarAsset, kInspectorAsset}) {
                WriteFile(_dir / Format("{}.xml", asset), ReadFile(ShippedFile(asset)));
            }
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _hostHandle = SceneEditorHostRegistry::GetInstance().Register(_host);
            _slot = PanelSlots::GetInstance().Register(kComponentsSlot, [this] {
                ++_slotCalls;
                const ImGuiWindow* window = ImGui::GetCurrentWindow();
                _slotInInspector = window != nullptr && window->Name != nullptr && std::strstr(window->Name, kInspectorChild) != nullptr;
            });

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());

            ASSERT_NO_FATAL_FAILURE(StartWidget(_toolbar, kToolbarAsset));
            ASSERT_NO_FATAL_FAILURE(StartWidget(_inspector, kInspectorAsset));
            Settle();
        }

        void TearDown() override {
            _toolbar.Reset();
            _inspector.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            _slot.Reset();
            _hostHandle.Reset();
            RemoveDirectory(_dir);
        }

        void StartWidget(IntrusivePtr<AssetPanelWidget>& widget, StringView asset) {
            widget = New<AssetPanelWidget>();
            widget->SetConfigManager(_manager);
            widget->SetAsset(asset);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(asset));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(widget->Initialize(*deserializer));
            ASSERT_TRUE(widget->GetHolder().Issues().empty());
        }

        /// What the editor does in a frame: the bars' widgets update, then the Scene Editor's window draws them
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _toolbar->Update();
            _inspector->Update();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            if (ImGui::Begin(kWindowName, nullptr, ImGuiWindowFlags_NoSavedSettings)) {
                PanelBarsRegistry::Draw(kToolbarAsset);
                ImGui::BeginChild(kInspectorChild, ImVec2(0.0f, 0.0f), true);
                PanelBarsRegistry::Draw(kInspectorAsset);
                ImGui::EndChild();
            }
            ImGui::End();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The visible item whose label ends with the element id @p id (`<text>##<id>`)
        const ImGuiItemRecord* Find(StringView id) {
            const String link = Format("##{}", id);
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && EndsWith(StringView(item.label.data(), item.label.size()), StringView(link.data(), link.size()))) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The visible item whose label holds @p text
        const ImGuiItemRecord* FindText(StringView text) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label.data(), item.label.size()).find(text) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The visible item labelled exactly @p label
        const ImGuiItemRecord* FindLabel(StringView label) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label.data(), item.label.size()) == label) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(const ImGuiItemRecord* item) {
            ASSERT_NE(item, nullptr);
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item->CenterX(), item->CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        void Click(StringView id) {
            ClickAt(Find(id));
        }

        void Type(StringView text) {
            ImGui::GetIO().AddInputCharactersUTF8(String(text.data(), text.size()).c_str());
            Settle();
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ProbeSceneEditorHost _host;
        SceneEditorHostHandle _hostHandle;
        PanelSlotHandle _slot;
        int _slotCalls = 0;
        bool _slotInInspector = false;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _toolbar;
        IntrusivePtr<AssetPanelWidget> _inspector;
        CommandRecorder _commands;
    };

    TEST_F(SceneEditorAssetTest, WithNoSelectionTheInspectorShowsNoFormAndNoComponents) {
        EXPECT_EQ(Find("node_name"), nullptr);
        EXPECT_EQ(Find("node_layer"), nullptr);
        EXPECT_EQ(FindText(kAddComponentText), nullptr);
        EXPECT_EQ(_slotCalls, 0);
    }

    TEST_F(SceneEditorAssetTest, TheNameFieldHoldsTheNodesNameAndTypingRenamesItThroughTheHost) {
        _host.SelectOne(kNodeName);
        Settle();
        ASSERT_NE(Find("node_name"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click("node_name"));
        Type(kTyped);

        const String expected = Format("{}{}", kNodeName, kTyped);
        ASSERT_FALSE(_host.renames.empty());
        EXPECT_EQ(_host.renames.back(), expected);
        EXPECT_EQ(_host.inspected.name.ToStringView(), StringView(expected.data(), expected.size()));
    }

    TEST_F(SceneEditorAssetTest, TheLayerStepperWritesTheNextLayer) {
        _host.SelectOne(kNodeName);
        _host.inspected.layer = kLayer;
        Settle();

        ASSERT_NO_FATAL_FAILURE(ClickAt(FindLabel("+")));

        EXPECT_EQ(_host.inspected.layer, kLayer + 1);
    }

    TEST_F(SceneEditorAssetTest, ALockedNodeTakesNoEditAndOffersNoAddComponent) {
        _host.SelectOne(kNodeName);
        _host.inspected.locked = true;
        Settle();

        ASSERT_NO_FATAL_FAILURE(Click("node_name"));
        Type(kTyped);
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindLabel("+")));
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindText(kAddComponentText)));

        EXPECT_TRUE(_host.renames.empty());
        EXPECT_TRUE(_host.layers.empty());
        EXPECT_EQ(_commands.Count(EditorCommandIds::kSceneEditorAddComponent), 0u);
        EXPECT_GT(_slotCalls, 0) << "a locked node still lists its components";
    }

    TEST_F(SceneEditorAssetTest, SaveIsDisabledWhileTheSceneIsCleanAndRunsSceneSaveWhileItIsDirty) {
        _host.SelectOne(kNodeName);
        Settle();
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindText(kSaveText)));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kSceneSave), 0u);

        _host.inspected.dirty = true;
        Settle();
        ASSERT_NO_FATAL_FAILURE(ClickAt(FindText(kSaveText)));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kSceneSave), 1u);
    }

    TEST_F(SceneEditorAssetTest, AddComponentRunsItsCommandAndFillsTheLine) {
        _host.SelectOne(kNodeName);
        Settle();
        const ImGuiItemRecord* button = FindText(kAddComponentText);
        ASSERT_NE(button, nullptr);

        const ImGuiWindow* inspector = nullptr;
        for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
            if (window->Name != nullptr && std::strstr(window->Name, kInspectorChild) != nullptr) {
                inspector = window;
            }
        }
        ASSERT_NE(inspector, nullptr);
        EXPECT_NEAR(button->minX, inspector->WorkRect.Min.x, kWidthTolerance);
        EXPECT_NEAR(button->maxX, inspector->WorkRect.Max.x, kWidthTolerance);

        ASSERT_NO_FATAL_FAILURE(ClickAt(button));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kSceneEditorAddComponent), 1u);
    }

    TEST_F(SceneEditorAssetTest, TheComponentsSlotIsDrawnInsideTheInspector) {
        _host.SelectOne(kNodeName);
        Settle();

        EXPECT_GT(_slotCalls, 0);
        EXPECT_TRUE(_slotInInspector);
    }

    TEST_F(SceneEditorAssetTest, ASteadyFrameAllocatesNothing) {
        _host.SelectOne(kNodeName);
        _host.inspected.selectionCount = 2;
        _host.inspected.sourceRef = PoolString::Intern(kNodeName);
        // The index keeps a record per item and is not what is measured
        _index.Disarm();
        Settle();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
