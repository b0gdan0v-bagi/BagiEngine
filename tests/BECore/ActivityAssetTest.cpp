#include <BECore/ActivityTestModel.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/SubscriptionHolder.h>
#include <Generated/ActivityTestModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 4;
        constexpr int kSteadyFrames = 8;
        constexpr StringView kPanelConfigName = "ActivityPanel";
        constexpr StringView kShippedModel = "model=\"ActivityPanelModel\"";
        constexpr StringView kTestModel = "model=\"ActivityTestModel\"";
        constexpr const char* kWindowName = "Activity###Activity";
        /// The label of the button holds its icon, a blank and its text
        constexpr StringView kClearButton = " Clear";

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ActivityPanel.xml";
        }

        String& Clipboard() {
            static String text;
            return text;
        }

    }  // namespace

    /// The shipped `ActivityPanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, over a control service of its own. The
    /// cells of its table are no items, so what is drawn is read through ImGui's text log
    class ActivityAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("activity_asset_panel");
            MakeEmptyDirectory(_dir);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "ActivityPanel.xml", asset);
            ASSERT_TRUE(_configFiles.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _service.RegisterHandler("scene_spawn", [](ControlCall& call) { call.ReplyEmpty(); });
            ActivityTestModel::service = &_service;

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
            ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { Clipboard() = text; };
            Clipboard().clear();
            _fonts = LoadImGuiFonts(io, _buffers);
            SetActiveImGuiFonts(_fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            _index.Arm(*ImGui::GetCurrentContext());

            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kPanelConfigName);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kPanelConfigName));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            ASSERT_TRUE(_widget->GetHolder().Issues().empty());
            Settle();
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            ActivityTestModel::service = nullptr;
            RemoveDirectory(_dir);
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _widget->Update();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The text the panel's window draws in one frame
        String DrawnText() {
            Clipboard().clear();
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            // The log ends with the first window that ends, which is the panel's
            ImGui::LogToClipboard();
            _widget->Update();
            ImGui::Render();
            return Clipboard();
        }

        void Open() {
            EditorCommandEvent::Emit(EditorCommandIds::kViewActivity);
            Settle();
        }

        void Call(StringView params) {
            _service.Post(MakeRequestText(1, "scene_spawn", params), [](const String&) {});
            _service.Drain();
        }

        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        void Click(StringView part) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ClickAt(*item);
        }

        void ClickAt(const ImGuiItemRecord& item) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item.CenterX(), item.CenterY());
            Frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            Frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            Settle();
        }

        Path _dir;
        FileSystem _configFiles;
        ConfigManager _manager{_configFiles};
        ControlService _service;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(ActivityAssetTest, ItIsHiddenUntilItsCommandAndSaysThatNothingHappened) {
        EXPECT_EQ(Find(kClearButton), nullptr);

        Open();

        EXPECT_NE(Find(kClearButton), nullptr);
        EXPECT_NE(DrawnText().find("No control API client"), String::npos);
    }

    TEST_F(ActivityAssetTest, ACallIsListedWithItsTextAsItIsAndClearEmptiesTheList) {
        Open();
        Call(R"({"name":"100%s ##x"})");
        Settle();

        const String listed = DrawnText();
        EXPECT_NE(listed.find("scene_spawn"), String::npos) << listed.c_str();
        EXPECT_NE(listed.find("100%s ##x"), String::npos) << "client text is neither a format string nor an ImGui label: " << listed.c_str();
        EXPECT_EQ(listed.find("No control API client"), String::npos);

        ASSERT_NO_FATAL_FAILURE(Click(kClearButton));

        EXPECT_EQ(_service.Activity().Size(), 0u);
        EXPECT_EQ(DrawnText().find("scene_spawn"), String::npos);
    }

    namespace {

        /// The arguments the panel's `activity.selectNode` buttons sent
        struct SelectNodeRecorder : SubscriptionHolder {
            SelectNodeRecorder() {
                Subscribe<EditorCommandEvent, &SelectNodeRecorder::OnCommand>(this);
            }

            void OnCommand(const EditorCommandEvent& event) {
                if (event.commandId == EditorCommandIds::kActivitySelectNode) {
                    arguments.push_back(event.argument);
                }
            }

            Vector<String> arguments;
        };

    }  // namespace

    TEST_F(ActivityAssetTest, OnlyTheButtonOfARowWithANodeAsksToSelectIt) {
        Open();
        constexpr uint64_t kNode = 42;
        _service.RegisterHandler("scene_touch", [this](ControlCall& call) {
            _service.ReportTouch(kNode);
            call.ReplyEmpty();
        });
        _service.Post(MakeRequestText(1, "scene_touch"), [](const String&) {}, _service.OpenLocalClient("agent"));
        Call(R"({})");  // the newer row, with no node
        Settle();
        Vector<ImGuiItemRecord> buttons;
        for (const ImGuiItemRecord& item : _index.Items()) {
            if (item.visible && StringView(item.label).find("##select") != StringView::npos) {
                buttons.push_back(item);
            }
        }
        ASSERT_EQ(buttons.size(), 2u);
        const SelectNodeRecorder recorder;

        ClickAt(buttons[0]);
        EXPECT_TRUE(recorder.arguments.empty());
        ClickAt(buttons[1]);

        ASSERT_EQ(recorder.arguments.size(), 1u);
        EXPECT_EQ(recorder.arguments.front(), "42");
    }

    TEST_F(ActivityAssetTest, AFrameOverAStandingJournalAllocatesNothing) {
        Open();
        Call(R"({"name":"hero"})");
        Settle();
        _index.Disarm();
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
