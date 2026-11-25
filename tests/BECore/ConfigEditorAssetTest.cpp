#include <BECore/Config/ConfigManager.h>
#include <BECore/ConfigEditorTestModel.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/ConfigEditorTestModel.gen.hpp>
#include <Widgets/ConfigEditor/ConfigFieldSchema.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr StringView kPanelConfigName = "ConfigEditorPanel";
        constexpr StringView kShippedModel = "model=\"ConfigEditorPanelModel\"";
        constexpr StringView kTestModel = "model=\"ConfigEditorTestModel\"";
        const PoolString kAlpha = "AlphaConfig"_intern;
        const PoolString kBeta = "BetaConfig"_intern;
        constexpr StringView kAlphaText = "<AlphaConfig value=\"one\"/>\n";
        constexpr StringView kBetaText = "<BetaConfig value=\"other\"/>\n";

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ConfigEditorPanel.xml";
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

    }  // namespace

    /// The shipped `ConfigEditorPanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, on a config folder of its own,
    /// found and driven the way a control client does it: by the labels of `ImGuiItemIndex`, with the mouse and the keyboard of ImGui
    class ConfigEditorAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("cfg_editor_asset");
            MakeEmptyDirectory(_dir);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "ConfigEditorPanel.xml", asset);
            WriteFile(_dir / "AlphaConfig.xml", kAlphaText);
            WriteFile(_dir / "BetaConfig.xml", kBetaText);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            ConfigEditorTestModel::manager = &_manager;

            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = kDisplaySize;
            io.IniFilename = nullptr;
            io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
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
            Settle();
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            ConfigEditorTestModel::manager = nullptr;
            ConfigFieldSchema::DestroyInstance();
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

        /// The first visible item whose label holds @p part
        const ImGuiItemRecord* Find(StringView part) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return &item;
                }
            }
            return nullptr;
        }

        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            Settle();
        }

        /// A click on the centre of the item, which must be there
        void Click(StringView part) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
        }

        /// The @p n-th visible item whose label holds @p part, in the order they were drawn
        const ImGuiItemRecord* FindNth(StringView part, size_t n) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part) && n-- == 0) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// Clicks into the input, types @p text after what it holds and confirms with Enter
        void EditInput(const ImGuiItemRecord* input, const char* text) {
            ASSERT_NE(input, nullptr);
            ClickAt(ImVec2(input->CenterX(), input->CenterY()));
            ImGuiIO& io = ImGui::GetIO();
            io.AddInputCharactersUTF8(text);
            Settle();
            io.AddKeyEvent(ImGuiKey_Enter, true);
            Frame();
            io.AddKeyEvent(ImGuiKey_Enter, false);
            Settle();
        }

        /// The row of `editor_state` the Config Editor reports for @p config, or null with no such row
        static Optional<EditorDocumentState> Document(StringView config) {
            const String id = Format("config:{}", config);
            for (EditorDocumentState& row : CoreManager::GetEditorDocuments().Collect()) {
                if (row.id == id) {
                    return std::move(row);
                }
            }
            return std::nullopt;
        }

        static bool IsDirty(StringView config) {
            const Optional<EditorDocumentState> row = Document(config);
            return row.has_value() && row->dirty;
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(ConfigEditorAssetTest, TheWindowIsTheNamedOneAndWithNoSelectionHoldsTheListOnly) {
        EXPECT_NE(ImGui::FindWindowByName("Config Editor###ConfigEditor"), nullptr);
        EXPECT_NE(Find("AlphaConfig##"), nullptr);
        EXPECT_NE(Find("BetaConfig##"), nullptr);
        EXPECT_EQ(Find("Save"), nullptr);
        EXPECT_EQ(Find("Reload"), nullptr);
        EXPECT_EQ(Find("##value"), nullptr);
        EXPECT_FALSE(Document("AlphaConfig").has_value());
    }

    TEST_F(ConfigEditorAssetTest, AClickOnARowSelectsItAndShowsTheButtonsAndTheTree) {
        ASSERT_NO_FATAL_FAILURE(Click("AlphaConfig##"));

        EXPECT_NE(Find("Save"), nullptr);
        EXPECT_NE(Find("Reload"), nullptr);
        EXPECT_NE(Find("##value"), nullptr);
        const Optional<EditorDocumentState> row = Document("AlphaConfig");
        ASSERT_TRUE(row.has_value());
        EXPECT_FALSE(row->dirty);
    }

    TEST_F(ConfigEditorAssetTest, AnEditInTheTreeIsStagedAndSaveWritesTheFile) {
        ASSERT_NO_FATAL_FAILURE(Click("AlphaConfig##"));

        ASSERT_NO_FATAL_FAILURE(EditInput(Find("##value"), "x"));

        EXPECT_TRUE(IsDirty("AlphaConfig"));
        EXPECT_FALSE(Contains(ReadFile(_dir / "AlphaConfig.xml"), "onex")) << "nothing is written before Save";

        ASSERT_NO_FATAL_FAILURE(Click("Save"));

        EXPECT_TRUE(Contains(ReadFile(_dir / "AlphaConfig.xml"), "onex"));
        EXPECT_FALSE(IsDirty("AlphaConfig"));
    }

    TEST_F(ConfigEditorAssetTest, SwitchingWithStagedEditsAsksFirstAndCancelKeepsTheSelection) {
        ASSERT_NO_FATAL_FAILURE(Click("AlphaConfig##"));
        ASSERT_NO_FATAL_FAILURE(EditInput(Find("##value"), "x"));

        ASSERT_NO_FATAL_FAILURE(Click("BetaConfig##"));

        EXPECT_NE(Find("Discard & Switch"), nullptr) << "the popup is open";
        ASSERT_NO_FATAL_FAILURE(Click("Cancel"));
        EXPECT_EQ(Find("Discard & Switch"), nullptr) << "the popup is closed";
        EXPECT_TRUE(IsDirty("AlphaConfig"));
        EXPECT_FALSE(Document("BetaConfig").has_value());

        ASSERT_NO_FATAL_FAILURE(Click("BetaConfig##"));
        ASSERT_NO_FATAL_FAILURE(Click("Discard & Switch"));

        EXPECT_EQ(Find("Discard & Switch"), nullptr);
        EXPECT_FALSE(Document("AlphaConfig").has_value());
        ASSERT_TRUE(Document("BetaConfig").has_value());
        EXPECT_FALSE(IsDirty("BetaConfig"));
        EXPECT_FALSE(Contains(ReadFile(_dir / "AlphaConfig.xml"), "onex"));
    }

    TEST_F(ConfigEditorAssetTest, ReloadWithStagedEditsAsksFirstAndDiscardReadsTheFileAgain) {
        ASSERT_NO_FATAL_FAILURE(Click("AlphaConfig##"));
        ASSERT_NO_FATAL_FAILURE(EditInput(Find("##value"), "x"));

        ASSERT_NO_FATAL_FAILURE(Click("Reload"));

        EXPECT_NE(Find("Discard & Reload"), nullptr) << "the popup is open";
        ASSERT_NO_FATAL_FAILURE(Click("Cancel"));
        EXPECT_TRUE(IsDirty("AlphaConfig"));

        WriteFile(_dir / "AlphaConfig.xml", "<AlphaConfig value=\"changed\"/>\n");
        ASSERT_NO_FATAL_FAILURE(Click("Reload"));
        ASSERT_NO_FATAL_FAILURE(Click("Discard & Reload"));

        EXPECT_FALSE(IsDirty("AlphaConfig"));
        EXPECT_STREQ(_manager.GetConfig(kAlpha).GetPugiNode().attribute("value").as_string(), "changed");
    }

    TEST_F(ConfigEditorAssetTest, SavingThePanelsOwnFileUnderThePanelKeepsThePanelAndItsSelection) {
        ASSERT_NO_FATAL_FAILURE(Click("ConfigEditorPanel##"));
        // The root's attributes come in the order of the file: id, title, ...
        ASSERT_NO_FATAL_FAILURE(EditInput(FindNth("##value", 1), "x"));
        ASSERT_TRUE(IsDirty("ConfigEditorPanel"));

        ASSERT_NO_FATAL_FAILURE(Click("Save"));

        EXPECT_TRUE(Contains(ReadFile(_dir / "ConfigEditorPanel.xml"), "Config Editorx"));
        EXPECT_NE(ImGui::FindWindowByName("###ConfigEditor"), nullptr);
        const Optional<EditorDocumentState> row = Document("ConfigEditorPanel");
        ASSERT_TRUE(row.has_value()) << "the model that runs was dropped by the reload of its own asset";
        EXPECT_FALSE(row->dirty);
        EXPECT_NE(Find("Save"), nullptr);
    }

}  // namespace BECore::Tests
