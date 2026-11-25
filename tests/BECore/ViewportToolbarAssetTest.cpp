#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <BECore/UI/UIPreview.h>
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
#include <Widgets/Viewport/ViewportToolbarState.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1600.0f, 720.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 10;
        constexpr int kMaxScrolls = 20;
        constexpr StringView kAsset = "ViewportToolbarPanel";
        constexpr const char* kHostName = "##Host";
        constexpr const char* kRowId = "preview";
        constexpr const char* kCustomRowText = "Custom...";
        constexpr const char* kFreeRowText = "Free";
        // A button with a text is labelled with its icon and text, under the element's id
        constexpr const char* kPlayText = "Play";
        constexpr const char* kEditText = "Edit";
        constexpr const char* kResetText = "Reset";
        constexpr ImVec2 kOutside{kDisplaySize.x - 4.0f, kDisplaySize.y - 4.0f};

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

        Path ShippedFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "ViewportToolbarPanel.xml";
        }

    }  // namespace

    /// The shipped Viewport toolbar hosted by a real `AssetPanelWidget` over its real model, drawn through `PanelBarsRegistry` into a host window as `ViewportWidget`
    /// draws it, in an ImGui context with no backend; the controls are found by the labels of `ImGuiItemIndex` and clicked with the mouse, and the choices are read
    /// through `IViewportToolbar`, the way the viewport reads them
    class ViewportToolbarAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("viewport_toolbar_config");
            MakeEmptyDirectory(_dir);
            WriteFile(_dir / "ViewportToolbarPanel.xml", ReadFile(ShippedFile()));
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();

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
            _widget->SetAsset(kAsset);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kAsset));
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
            RemoveDirectory(_dir);
        }

        /// The model the widget made, as the viewport sees it
        static IViewportToolbar& Toolbar() {
            IViewportToolbar* toolbar = ViewportToolbarRegistry::GetInstance().Get();
            EXPECT_NE(toolbar, nullptr);
            return toolbar != nullptr ? *toolbar : ViewportToolbarRegistry::GetInstance().GetOrDefault();
        }

        /// What the editor does in a frame: the widget updates, then the viewport's window draws the bar
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            _widget->Update();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kDisplaySize);
            constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
            if (ImGui::Begin(kHostName, nullptr, hostFlags)) {
                PanelBarsRegistry::Draw(kAsset);
            }
            ImGui::End();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// The visible item whose label holds @p text (an element id is written `##<id>`)
        const ImGuiItemRecord* FindText(StringView text) {
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && item.label.find(text.data(), 0, text.size()) != String::npos) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The visible item of the element @p id: a combo and a number are recorded with no label, so they are found by the chain of ids the interpreter
        /// pushes (the row, the element, the control's own `##<id>`); a dropdown, a checkbox and a table cell by the `##<id>` of their label
        const ImGuiItemRecord* Find(StringView id) {
            const ImGuiWindow* host = ImGui::FindWindowByName(kHostName);
            if (host != nullptr) {
                const String element(id.data(), id.size());
                const String control = Format("##{}", id);
                const ImGuiID wanted = ImHashStr(control.c_str(), 0, ImHashStr(element.c_str(), 0, ImHashStr(kRowId, 0, host->ID)));
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && item.id == wanted) {
                        return &item;
                    }
                }
            }
            return FindText(Format("##{}", id));
        }

        void ClickAt(float x, float y) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(x, y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        void ClickText(StringView text) {
            const ImGuiItemRecord* item = FindText(text);
            ASSERT_NE(item, nullptr) << String(text.data(), text.size()).c_str();
            ClickAt(item->CenterX(), item->CenterY());
        }

        void Click(StringView id) {
            const ImGuiItemRecord* item = Find(id);
            ASSERT_NE(item, nullptr) << String(id.data(), id.size()).c_str();
            ClickAt(item->CenterX(), item->CenterY());
        }

        /// Opens the size combo and clicks the row whose text holds @p text; @p last scrolls the list to its end first, where Custom... is
        void PickSize(StringView text, bool last = false) {
            ASSERT_NO_FATAL_FAILURE(Click("size"));
            if (last) {
                const ImGuiItemRecord* first = FindText(kFreeRowText);
                ASSERT_NE(first, nullptr);
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(first->CenterX(), first->CenterY());
                for (int i = 0; i < kMaxScrolls; ++i) {
                    io.AddMouseWheelEvent(0.0f, -1.0f);
                    Frame();
                }
                Settle();
            }
            ASSERT_NO_FATAL_FAILURE(ClickText(text));
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
        CommandRecorder _commands;
    };

    TEST_F(ViewportToolbarAssetTest, ADeviceRowOfTheSizeComboIsTheShapeAndBringsTheDeviceSafeArea) {
        const Span<const PreviewDevice> devices = UIPreview::GetDevices();
        ASSERT_FALSE(devices.empty());
        ASSERT_NO_FATAL_FAILURE(PickSize(devices[0].name));

        const PreviewSettings preview = Toolbar().GetPreview();
        EXPECT_EQ(preview.shape.device, 0);
        EXPECT_FALSE(preview.shape.custom);
        EXPECT_EQ(preview.safeArea, SafeAreaSimulation::Device);

        ASSERT_NO_FATAL_FAILURE(PickSize(kFreeRowText));
        EXPECT_EQ(Toolbar().GetPreview().shape.device, -1);
        EXPECT_EQ(Toolbar().GetPreview().safeArea, SafeAreaSimulation::None) << "another size drops the device's safe area";
    }

    TEST_F(ViewportToolbarAssetTest, RotateDoesNothingForTheFreeShapeAndTurnsAnAspect) {
        ASSERT_NO_FATAL_FAILURE(Click("rotate"));
        EXPECT_FALSE(Toolbar().GetPreview().shape.rotated) << "Rotate is disabled for Free";

        ASSERT_NO_FATAL_FAILURE(PickSize("16:9"));
        ASSERT_NO_FATAL_FAILURE(Click("rotate"));
        EXPECT_TRUE(Toolbar().GetPreview().shape.rotated);
    }

    TEST_F(ViewportToolbarAssetTest, TheCustomSidesAreShownOnlyForCustomAndATypedSideIsClampedOnEnter) {
        EXPECT_EQ(Find("customWidth"), nullptr);
        EXPECT_EQ(Find("customHeight"), nullptr);

        ASSERT_NO_FATAL_FAILURE(PickSize(kCustomRowText, true));
        ASSERT_TRUE(Toolbar().GetPreview().shape.custom);
        ASSERT_NE(Find("customHeight"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click("customWidth"));
        ImGuiIO& io = ImGui::GetIO();
        io.AddInputCharactersUTF8("9999");
        Settle();
        EXPECT_EQ(Toolbar().GetPreview().shape.customWidth, PreviewShape{}.customWidth) << "typing alone changes nothing";

        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Settle();
        EXPECT_EQ(Toolbar().GetPreview().shape.customWidth, kMaxPreviewSide);
    }

    TEST_F(ViewportToolbarAssetTest, TheOverlaysDropdownOpensOnItsButtonAndASwitchInItChangesThePreview) {
        EXPECT_EQ(Find("viewBounds"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click("overlays"));
        ASSERT_NE(Find("viewBounds"), nullptr);
        EXPECT_NE(Find("landscape"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click("viewBounds"));
        EXPECT_TRUE(Toolbar().GetPreview().overlays.viewBounds);
        EXPECT_NE(Find("viewBounds"), nullptr) << "a click inside keeps the popup open";

        ClickAt(kOutside.x, kOutside.y);
        EXPECT_EQ(Find("viewBounds"), nullptr);
    }

    TEST_F(ViewportToolbarAssetTest, ADeviceSwitchOfTheOverlaysChecksItsShape) {
        const Span<const PreviewDevice> devices = UIPreview::GetDevices();
        ASSERT_FALSE(devices.empty());
        ASSERT_NO_FATAL_FAILURE(Click("overlays"));
        ASSERT_NO_FATAL_FAILURE(ClickText(Format("{}##checked", devices[0].name)));

        EXPECT_EQ(Toolbar().GetShapeMask(), uint64_t{1});
    }

    TEST_F(ViewportToolbarAssetTest, ThePlayAndEditButtonsFlipTheInputMode) {
        EXPECT_EQ(FindText(kEditText), nullptr);
        ASSERT_NO_FATAL_FAILURE(ClickText(kPlayText));
        EXPECT_EQ(Toolbar().GetInputMode(), ViewportInputMode::Edit);
        EXPECT_EQ(FindText(kPlayText), nullptr);

        ASSERT_NO_FATAL_FAILURE(ClickText(kEditText));
        EXPECT_EQ(Toolbar().GetInputMode(), ViewportInputMode::Play);
        EXPECT_EQ(_commands.Count(EditorCommandIds::kViewportInputMode), 2u);
    }

    TEST_F(ViewportToolbarAssetTest, ResetIsDisabledUntilTheCameraMoved) {
        ASSERT_NO_FATAL_FAILURE(ClickText(kResetText));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kViewportCameraReset), 0u);

        Toolbar().PublishCamera(100.0f, true);
        ASSERT_NO_FATAL_FAILURE(ClickText(kResetText));
        EXPECT_EQ(_commands.Count(EditorCommandIds::kViewportCameraReset), 1u);
    }

    TEST_F(ViewportToolbarAssetTest, ASteadyFrameWithTheOverlaysOpenAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Click("overlays"));
        ASSERT_NE(Find("viewBounds"), nullptr);
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
