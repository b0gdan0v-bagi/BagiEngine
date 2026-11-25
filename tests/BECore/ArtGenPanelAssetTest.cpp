#include <BECore/ArtGenPanelTestModel.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/Control/ControlService.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <Common/AllocCheck.h>
#include <Common/ControlTestSupport.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/ArtGenPanelTestModel.gen.hpp>
#include <Widgets/ArtGen/ArtGenImage.h>
#include <Widgets/ArtGen/ArtGenPresetForm.h>
#include <Widgets/ArtGen/ArtGenSession.h>
#include <Widgets/ArtGen/ArtGenSound.h>
#include <Widgets/ArtGen/Reference/ArtGenReference.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 1000.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 8;
        constexpr int kFrameGuard = 100;
        constexpr uint16_t kPort = 4567;
        constexpr const char* kWindowName = "Generate###Generate";
        constexpr StringView kPanelConfigName = "GeneratePanel";
        constexpr StringView kShippedModel = "model=\"ArtGenPanelModel\"";
        constexpr StringView kTestModel = "model=\"ArtGenPanelTestModel\"";
        /// The game's preset `wood`, the engine's starter `sprite`, a broken one, and one workflow
        constexpr StringView kCatalogPoll =
            R"({"generator":"fake","presets":[{"name":"wood","preset":{"preset":1,"prompt":"carved {subject}","title":"Wood","workflow":"sdxl_basic"},"source":"game","title":"wood"},)"
            R"({"name":"sprite","preset":{"preset":1,"prompt":"{subject}","title":"Sprite","workflow":"sdxl_basic"},"source":"engine","title":"sprite"},)"
            R"({"error":"no workflow 'x'","name":"broken","title":"broken"}],"state":"ready",)"
            R"("workflows":[{"inputs":{"height":{"default":768},"sampler":{"choices":["euler","ddim"],"default":"euler"},"width":{"default":1024}},)"
            R"("name":"sdxl_basic","source":"engine"}]})";

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "GeneratePanel.xml";
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// The number element @p id of the tree, or null
        const PanelNumber* FindNumber(const Vector<IntrusivePtr<PanelElement>>& elements, StringView id) {
            for (const IntrusivePtr<PanelElement>& element : elements) {
                if (element->id == id) {
                    return element->Cast<PanelNumber>();
                }
                if (const PanelNumber* inner = FindNumber(element->elements, id)) {
                    return inner;
                }
            }
            return nullptr;
        }

    }  // namespace

    /// The shipped `GeneratePanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, over a session, a control service and
    /// a control status of the test's own, driven by the labels of `ImGuiItemIndex`
    class ArtGenPanelAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        ArtGenPanelAssetTest() : _session([] { return 0.0; }) {}

        void SetUp() override {
            _dir = TestTempDirectory("generate_panel_asset");
            MakeEmptyDirectory(_dir);
            _game = _dir / "game";
            MakeDirectory(_game);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "GeneratePanel.xml", asset);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            ArtGenPanelTestModel::session = &_session;
            ArtGenPanelTestModel::control = &_service;
            ArtGenPanelTestModel::status = &_control;
            ArtGenPanelTestModel::controlStarts = 0;

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
            ASSERT_NE(ArtGenPanelTestModel::instance, nullptr);
            _session.SetGameRoot(_game.View());
            EditorCommandEvent::Emit(EditorCommandIds::kViewGenerate);
            Settle();
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            ArtGenPanelTestModel::session = nullptr;
            ArtGenPanelTestModel::control = nullptr;
            ArtGenPanelTestModel::status = nullptr;
            RemoveDirectory(_dir);
        }

        static const ArtGenPanelView& View() {
            return ArtGenPanelTestModel::instance->GetView();
        }

        /// The window fills the display, so no control is clipped away
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetWindowPos(kWindowName, ImVec2(0.0f, 0.0f));
            ImGui::SetWindowSize(kWindowName, kDisplaySize);
            _widget->Update();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        /// One control call through the service, as the bridge makes it
        void Call(StringView method, StringView params) {
            bool answered = false;
            _service.Post(MakeRequestText(++_nextId, method, params), [&](const String& /*reply*/) { answered = true; });
            for (int frame = 0; !answered && frame < kFrameGuard; ++frame) {
                _service.Drain();
                _service.EndFrame();
            }
        }

        /// The control API listens and the service polled with its catalog: the model loads the first preset
        void Connect() {
            _control.listening = true;
            _control.port = kPort;
            Call("artgen_poll", kCatalogPoll);
            Settle();
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

        /// The visible item drawn just before the one whose label holds @p part: ImGui gives the index no label for a closed combo
        const ImGuiItemRecord* ItemBefore(StringView part) {
            const ImGuiItemRecord* previous = nullptr;
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, part)) {
                    return previous;
                }
                previous = item.visible ? &item : previous;
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

        void ClickItem(const ImGuiItemRecord* item, StringView what) {
            ASSERT_NE(item, nullptr) << String(what).c_str();
            ClickAt(ImVec2(item->CenterX(), item->CenterY()));
        }

        /// A click on the centre of the item, which must be there
        void Click(StringView part) {
            ClickItem(Find(part), part);
        }

        /// The Set tab, found by ImGui's own tab list: a tab item is not in the item index
        void ClickSetTab() {
            const ImGuiTabBar* bar = nullptr;
            for (const ImGuiTabBar& candidate : ImGui::GetCurrentContext()->TabBars.Buf) {
                bar = bar == nullptr && candidate.Tabs.Size == 2 ? &candidate : bar;
            }
            ASSERT_NE(bar, nullptr);
            const ImGuiTabItem& tab = bar->Tabs[1];
            ClickAt(ImVec2(bar->BarRect.Min.x + tab.Offset + tab.Width * 0.5f, (bar->BarRect.Min.y + bar->BarRect.Max.y) * 0.5f));
        }

        /// Clicks into the input and types @p text after what it holds
        void Type(StringView part, const char* text) {
            ASSERT_NO_FATAL_FAILURE(Click(part));
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
        }

        Path _dir;
        Path _game;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        ControlServerStatus _control;
        ControlService _service;
        ArtGenSession _session;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
        int64_t _nextId = 0;
    };

    TEST_F(ArtGenPanelAssetTest, WithTheControlApiOffStartTurnsItOn) {
        EXPECT_NE(ImGui::FindWindowByName(kWindowName), nullptr);
        EXPECT_EQ(Find(ICON_LC_UNPLUG " Disconnect"), nullptr);

        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_POWER " Start the service"));

        EXPECT_EQ(ArtGenPanelTestModel::controlStarts, 1);
        EXPECT_TRUE(View().service.controlFailed) << "the control API is still off";
    }

    TEST_F(ArtGenPanelAssetTest, TheComboListsThePresetsAndABrokenOneCannotBePicked) {
        Connect();
        EXPECT_NE(Find(ICON_LC_UNPLUG " Disconnect"), nullptr);
        EXPECT_EQ(Find(ICON_LC_POWER " Start the service"), nullptr);
        ASSERT_EQ(View().preset.name, "wood");

        ASSERT_NO_FATAL_FAILURE(ClickItem(ItemBefore("##newPreset"), "the preset combo"));
        EXPECT_NE(Find("sprite  (sprite, engine)"), nullptr);
        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_CIRCLE_ALERT " broken  (broken)"));

        EXPECT_EQ(View().preset.index, 0);
        EXPECT_EQ(View().preset.name, "wood");
    }

    TEST_F(ArtGenPanelAssetTest, SaveAsFillsTheNameOnOpenAndClosesOnSave) {
        Connect();

        ASSERT_NO_FATAL_FAILURE(Click("##savePresetAs"));
        EXPECT_EQ(View().preset.saveAsName, "wood") << "the dropdown's open command fills the name from the form";
        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_SAVE " Save"));

        EXPECT_TRUE(PathExists(_game / "artgen" / "presets" / "wood.json"));
        EXPECT_EQ(Find(ICON_LC_SAVE " Save"), nullptr) << "Save closes the dropdown";
    }

    TEST_F(ArtGenPanelAssetTest, GenerateIsOffUntilASubjectIsTyped) {
        Connect();

        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_SPARKLES " Generate"));
        EXPECT_EQ(_session.GetRun().state, ArtGenRunState::Idle);

        ASSERT_NO_FATAL_FAILURE(Type("##subject", "knight"));
        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_SPARKLES " Generate"));
        EXPECT_EQ(_session.GetRun().state, ArtGenRunState::Queued);
        EXPECT_EQ(_session.GetRun().subject, "knight");
    }

    TEST_F(ArtGenPanelAssetTest, TheSetTabDrawsItsSlotInsteadOfTheGenerateTab) {
        Connect();
        EXPECT_NE(Find("##referenceFile"), nullptr);
        EXPECT_EQ(Find(ICON_LC_LIST_PLUS " Queue"), nullptr);

        ASSERT_NO_FATAL_FAILURE(ClickSetTab());

        EXPECT_NE(Find(ICON_LC_LIST_PLUS " Queue"), nullptr);
        EXPECT_EQ(Find("##referenceFile"), nullptr);
    }

    TEST_F(ArtGenPanelAssetTest, TheLimitsOfTheAssetAreTheCodes) {
        const PanelLoadResult loaded = LoadPanelAsset(StringView(ReadFile(ShippedAssetFile())));
        ASSERT_NE(loaded.asset, nullptr);
        EXPECT_TRUE(loaded.issues.empty());
        const auto& elements = loaded.asset->elements;
        const auto expectRange = [&](StringView id, float min, float max) {
            const PanelNumber* number = FindNumber(elements, id);
            ASSERT_NE(number, nullptr) << String(id).c_str();
            EXPECT_FLOAT_EQ(number->min, min) << String(id).c_str();
            EXPECT_FLOAT_EQ(number->max, max) << String(id).c_str();
        };
        expectRange("count", 1.0f, static_cast<float>(ArtGenSession::kMaxCount));
        expectRange("width", 1.0f, static_cast<float>(ArtGenPresetForm::kMaxImageSide));
        expectRange("height", 1.0f, static_cast<float>(ArtGenPresetForm::kMaxImageSide));
        for (const StringView id : {StringView("presetPadding"), StringView("acceptPadding")}) {
            expectRange(id, static_cast<float>(ArtGenImageSettings::kMinPadding), static_cast<float>(ArtGenImageSettings::kMaxPadding));
        }
        expectRange("trimDb", ArtGenSoundSettings::kMinTrimDb, ArtGenSoundSettings::kMaxTrimDb);
        expectRange("peakDb", ArtGenSoundSettings::kMinPeakDb, ArtGenSoundSettings::kMaxPeakDb);
        expectRange("fadeIn", 0.0f, ArtGenSoundSettings::kMaxFadeMs);
        expectRange("fadeOut", 0.0f, ArtGenSoundSettings::kMaxFadeMs);
        expectRange("maxSeconds", 0.0f, ArtGenSoundSettings::kMaxSeconds);
        const ArtGenReferenceModeRow& init = ArtGenReferenceModeOf("init");
        const ArtGenReferenceModeRow& style = ArtGenReferenceModeOf("style");
        const ArtGenReferenceModeRow& structure = ArtGenReferenceModeOf("structure");
        EXPECT_FLOAT_EQ(style.minWeight, structure.minWeight) << "one slider serves both";
        EXPECT_FLOAT_EQ(style.maxWeight, structure.maxWeight) << "one slider serves both";
        for (const StringView id : {StringView("presetRefInitWeight"), StringView("referenceInitWeight")}) {
            expectRange(id, init.minWeight, init.maxWeight);
        }
        for (const StringView id : {StringView("presetRefWeight"), StringView("referenceWeight")}) {
            expectRange(id, style.minWeight, style.maxWeight);
        }
    }

    TEST_F(ArtGenPanelAssetTest, ASteadyFrameWithThePresetSettingsOpenAllocatesNothing) {
        Connect();
        ASSERT_NO_FATAL_FAILURE(Click("###settings"));
        ASSERT_NE(Find("###presetAccept"), nullptr) << "the Settings section is open";
        Settle();

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
