// The shipped FieldHelpPanel.xml in a real widget: the click on a row, the multiline field and Save.

#include <BECore/Config/ConfigManager.h>
#include <BECore/FieldHelpPanelTestModel.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/FieldHelpPanelTestModel.gen.hpp>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 4;
        constexpr int kSteadyFrames = 8;
        constexpr StringView kPanelConfigName = "FieldHelpPanel";
        constexpr StringView kShippedModel = "model=\"FieldHelpPanelModel\"";
        constexpr StringView kTestModel = "model=\"FieldHelpPanelTestModel\"";
        constexpr StringView kProbe = "HelpProbeComponent";
        /// The labels of the cells of the name column, of the multiline field and of the buttons (an icon, a blank, the text)
        constexpr StringView kCountRow = "count##select";
        constexpr StringView kTitleRow = "title##select";
        constexpr StringView kDescriptionField = "##text";
        constexpr StringView kSaveButton = " Save";
        /// More than the text buffer of a panel's input holds
        constexpr size_t kLongTextBytes = 3000;

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "FieldHelpPanel.xml";
        }

        class FieldHelpAssetTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                _dir = TestTempDirectory("field_help_asset_panel");
                MakeEmptyDirectory(_dir);
                String asset = ReadFile(ShippedAssetFile());
                const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
                ASSERT_NE(at, String::npos);
                asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
                WriteFile(_dir / "FieldHelpPanel.xml", asset);
                ASSERT_TRUE(_configFiles.Mount(MountPoints::config, _dir));
                _manager.Initialize();
                FieldHelpPanelTestModel::files = &_configFiles;

                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                _fonts = LoadImGuiFonts(io, _buffers);
                SetActiveImGuiFonts(_fonts);
                ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
                _index.Arm(*ImGui::GetCurrentContext());
            }

            void TearDown() override {
                _widget.Reset();
                _index.Disarm();
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
                FieldHelpPanelTestModel::files = nullptr;
                FieldHelpPanelTestModel::made = nullptr;
                RemoveDirectory(_dir);
            }

            [[nodiscard]] Path HelpFile() const {
                return _dir / "FieldHelpConfig.xml";
            }

            void Start() {
                _widget = New<AssetPanelWidget>();
                _widget->SetConfigManager(_manager);
                _widget->SetAsset(kPanelConfigName);
                const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kPanelConfigName));
                ASSERT_TRUE(deserializer);
                ASSERT_TRUE(_widget->Initialize(*deserializer));
                ASSERT_TRUE(_widget->GetHolder().Issues().empty());
                ASSERT_NE(FieldHelpPanelTestModel::made, nullptr);
                Settle();
            }

            FieldHelpPanelView& View() {
                return FieldHelpPanelTestModel::made->GetView();
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

            /// Opens the panel on the probe component
            void Open() {
                EditorCommandEvent::Emit(EditorCommandIds::kViewFieldHelp);
                Settle();
                for (size_t i = 0; i < View().classes.size(); ++i) {
                    if (StringView(View().classes[i]) == kProbe) {
                        View().classIndex = static_cast<int32_t>(i);
                    }
                }
                Settle();
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
                const ImGuiItemRecord* const item = Find(part);
                ASSERT_NE(item, nullptr) << String(part).c_str();
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(item->CenterX(), item->CenterY());
                Frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                Frame();
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                Settle();
            }

            void TypeText(const char* text) {
                ImGui::GetIO().AddInputCharactersUTF8(text);
                Settle();
            }

            Path _dir;
            FileSystem _configFiles;
            ConfigManager _manager{_configFiles};
            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
            ImGuiItemIndex _index;
            IntrusivePtr<AssetPanelWidget> _widget;
        };

    }  // namespace

    TEST_F(FieldHelpAssetTest, ARowIsSelectedByAClickWhatIsTypedIsItsEntryAndSaveWritesIt) {
        ASSERT_NO_FATAL_FAILURE(Start());
        EXPECT_EQ(Find(kSaveButton), nullptr) << "hidden until its command";
        Open();
        EXPECT_EQ(Find(kDescriptionField), nullptr) << "nothing is selected";

        ASSERT_NO_FATAL_FAILURE(Click(kCountRow));
        EXPECT_EQ(View().selectedField, "count");
        ASSERT_NO_FATAL_FAILURE(Click(kDescriptionField));
        TypeText("how many");
        EXPECT_TRUE(View().dirty);
        // The click on another row takes the field's focus: its text must stay with the row it was typed for
        ASSERT_NO_FATAL_FAILURE(Click(kTitleRow));
        EXPECT_EQ(View().selectedField, "title");
        EXPECT_EQ(View().description, "");

        ASSERT_NO_FATAL_FAILURE(Click(kSaveButton));

        EXPECT_FALSE(View().dirty);
        const String saved = ReadFile(HelpFile());
        EXPECT_NE(saved.find("how many"), String::npos) << saved.c_str();
        EXPECT_NE(saved.find("field=\"count\""), String::npos) << saved.c_str();
        EXPECT_EQ(saved.find("field=\"title\""), String::npos) << saved.c_str();
    }

    TEST_F(FieldHelpAssetTest, ATextLongerThanTheFieldIsNotCutBySelectingItAndVisitingTheField) {
        const String text(kLongTextBytes, 'a');
        const String file = Format("<root><entries><item component=\"{}\" field=\"count\" text=\"{}\"/></entries></root>", kProbe, text);
        WriteFile(HelpFile(), file);
        ASSERT_NO_FATAL_FAILURE(Start());
        Open();

        ASSERT_NO_FATAL_FAILURE(Click(kCountRow));
        ASSERT_NO_FATAL_FAILURE(Click(kDescriptionField));
        ASSERT_NO_FATAL_FAILURE(Click(kTitleRow));
        ASSERT_NO_FATAL_FAILURE(Click(kCountRow));

        EXPECT_EQ(View().description, text);
        EXPECT_FALSE(View().dirty);
    }

    TEST_F(FieldHelpAssetTest, ACommandReachesTheModelOnlyWhileThePanelIsOpen) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Open();
        EditorCommandEvent::Emit(EditorCommandIds::kViewFieldHelp);
        Settle();

        EditorCommandEvent::Emit(EditorCommandIds::kFieldHelpSelect, String("count"));
        Settle();

        EXPECT_FALSE(View().hasSelection);
    }

    TEST_F(FieldHelpAssetTest, ASettledFrameAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start());
        Open();
        ASSERT_NO_FATAL_FAILURE(Click(kCountRow));
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
