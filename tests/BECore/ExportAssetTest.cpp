#include <BECore/Config/ConfigManager.h>
#include <BECore/ExportTestModel.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/ExportTestModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr StringView kLockedScene = "Menu";
        constexpr StringView kFreeScene = "Level";
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 8;
        /// More lines than the window shows at once
        constexpr int kManyLines = 200;
        constexpr StringView kPanelConfigName = "ExportPanel";
        constexpr StringView kShippedModel = "model=\"ExportPanelModel\"";
        constexpr StringView kTestModel = "model=\"ExportTestModel\"";
        constexpr StringView kLineLabel = "##line";
        /// The label of a button holds its icon, a blank and its text
        constexpr StringView kExportButton = " Export";

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ExportPanel.xml";
        }

        String& Clipboard() {
            static String text;
            return text;
        }

    }  // namespace

    /// The shipped `ExportPanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, over a model with a fake process, driven by
    /// the labels of `ImGuiItemIndex`
    class ExportAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("export_asset_panel");
            MakeEmptyDirectory(_dir);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "ExportPanel.xml", asset);
            _fixture.WriteScenes({{kLockedScene, kFreeScene}});
            ASSERT_TRUE(_configFiles.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            ExportTestModel::fixture = &_fixture;

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
            // The asset keeps the window hidden
            EditorCommandEvent::Emit(EditorCommandIds::kViewExport);
            Settle();
            ASSERT_NE(ExportTestModel::instance, nullptr);
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            ExportTestModel::fixture = nullptr;
            RemoveDirectory(_dir);
        }

        static const ExportPanelView& View() {
            return ExportTestModel::instance->GetView();
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

        /// The first visible item whose label holds @p part
        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// Whether @p name is among the arguments of the export that was started, after `--exclude-scene`
        bool LeftOut(StringView name) const {
            const Vector<String>& arguments = _fixture.launcher.started;
            for (size_t i = 0; i + 1 < arguments.size(); ++i) {
                if (arguments[i] == "--exclude-scene" && StringView(arguments[i + 1]) == name) {
                    return true;
                }
            }
            return false;
        }

        /// The rows of the output list that are in view
        size_t VisibleLines() {
            size_t count = 0;
            for (const ImGuiItemRecord& item : _index.Items()) {
                count += item.visible && StringView(item.label) == kLineLabel ? 1 : 0;
            }
            return count;
        }

        /// A click on the centre of the item, which must be there
        void Click(StringView part, int button = ImGuiMouseButton_Left) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item->CenterX(), item->CenterY());
            Frame();
            io.AddMouseButtonEvent(button, true);
            Frame();
            io.AddMouseButtonEvent(button, false);
            Frame();
            Settle();
        }

        Path _dir;
        FileSystem _configFiles;
        ConfigManager _manager{_configFiles};
        ExportFixture _fixture{"export_asset"};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(ExportAssetTest, TheButtonsRunTheirCommandsAndFollowTheState) {
        EXPECT_EQ(Find(" Open folder"), nullptr);
        EXPECT_EQ(Find(" Run"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click(" Cancel"));
        EXPECT_EQ(_fixture.launcher.starts, 0);

        ASSERT_NO_FATAL_FAILURE(Click("Release"));
        EXPECT_EQ(View().config, ExportConfig::Release);
        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        ASSERT_EQ(_fixture.launcher.starts, 1);

        // While it runs the choices and Export are off
        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        ASSERT_NO_FATAL_FAILURE(Click("Debug"));
        EXPECT_EQ(_fixture.launcher.starts, 1);
        EXPECT_EQ(View().config, ExportConfig::Release);

        ASSERT_NO_FATAL_FAILURE(Click(" Cancel"));
        EXPECT_TRUE(_fixture.launcher.run.cancelled);
        _fixture.launcher.End(kExportExitCancelled);
        Settle();
        EXPECT_EQ(ExportTestModel::instance->GetState(), ExportState::Cancelled);

        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        _fixture.launcher.End(0, "== export ok: /pkg\n");
        Settle();
        ASSERT_NO_FATAL_FAILURE(Click(" Open folder"));
        EXPECT_EQ(_fixture.openedUrl, "file:///pkg");
        ASSERT_NO_FATAL_FAILURE(Click(" Run"));
        EXPECT_EQ(_fixture.launcher.detachedStarts, 1);
    }

    TEST_F(ExportAssetTest, TheTargetAndTheScenesOfThePanelReachTheExport) {
        ASSERT_NO_FATAL_FAILURE(Click("Web"));
        EXPECT_EQ(View().platform, ExportPlatform::Web);
        ASSERT_NO_FATAL_FAILURE(Click(kLockedScene));
        ASSERT_NO_FATAL_FAILURE(Click(kFreeScene));
        ASSERT_EQ(View().scenes.size(), 2u);
        EXPECT_TRUE(View().scenes[0].included) << "the checkbox of a locked scene takes no click";
        EXPECT_FALSE(View().scenes[1].included);

        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        ASSERT_EQ(_fixture.launcher.starts, 1);
        EXPECT_TRUE(LeftOut(kFreeScene));
        EXPECT_FALSE(LeftOut(kLockedScene));

        // While it runs the scenes are off
        ASSERT_NO_FATAL_FAILURE(Click(kFreeScene));
        EXPECT_FALSE(View().scenes[1].included);
    }

    TEST_F(ExportAssetTest, CheckAssetsFillsTheFindings) {
        WriteFile(_fixture.root / "config" / "Probe.xml", "<Probe path=\"assets/gone.png\"/>\n");
        EXPECT_FALSE(View().checked);

        ASSERT_NO_FATAL_FAILURE(Click(" Check assets"));

        EXPECT_TRUE(View().checked);
        ASSERT_EQ(View().findings.size(), 1u);
        EXPECT_EQ(View().findings[0].path, "assets/gone.png");
        EXPECT_EQ(_fixture.launcher.starts, 0);
    }

    TEST_F(ExportAssetTest, StopIsThereOnlyWhileAWebPageIsServed) {
        ASSERT_NO_FATAL_FAILURE(Click("Web"));
        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        _fixture.launcher.End(0, "== export ok: /pkg\n");
        Settle();
        EXPECT_EQ(Find(" Stop"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click(" Run"));
        ASSERT_EQ(_fixture.launcher.serverStarts, 1);
        ASSERT_NO_FATAL_FAILURE(Click(" Run"));
        EXPECT_EQ(_fixture.launcher.serverStarts, 1);

        ASSERT_NO_FATAL_FAILURE(Click(" Stop"));
        EXPECT_TRUE(_fixture.launcher.server.cancelled);
        _fixture.launcher.server.exitCode = kExportExitCancelled;
        Settle();
        EXPECT_EQ(Find(" Stop"), nullptr);
    }

    TEST_F(ExportAssetTest, ErrorsOnlyHidesTheOtherLinesAndALineIsCopiedFromItsMenu) {
        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        _fixture.launcher.Write("plain\nA.cpp:1: warning: w\nA.cpp:2: error: the broken one\nlast\n");
        Settle();
        EXPECT_EQ(VisibleLines(), 4u);

        ASSERT_NO_FATAL_FAILURE(Click("Errors only"));
        EXPECT_EQ(VisibleLines(), 1u);

        // The one line left is the error
        ASSERT_NO_FATAL_FAILURE(Click(kLineLabel, ImGuiMouseButton_Right));
        ASSERT_NO_FATAL_FAILURE(Click("Copy line"));
        EXPECT_EQ(Clipboard(), "A.cpp:2: error: the broken one");

        // A line that arrives is filtered too
        _fixture.launcher.Write("B.cpp:3: error: another\nmore\n");
        Settle();
        EXPECT_EQ(VisibleLines(), 2u);
    }

    TEST_F(ExportAssetTest, AFrameWithNoNewLineAllocatesNothing) {
        WriteFile(_fixture.root / "config" / "Probe.xml", "<Probe path=\"assets/gone.png\"/>\n");
        ASSERT_NO_FATAL_FAILURE(Click(kExportButton));
        ASSERT_TRUE(View().hasFindings);
        String lines;
        for (int i = 0; i < kManyLines; ++i) {
            lines += Format("{}: error: e\n", i);
        }
        _fixture.launcher.Write(lines);
        ASSERT_NO_FATAL_FAILURE(Click("Errors only"));
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        _index.Disarm();
        Settle();
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
