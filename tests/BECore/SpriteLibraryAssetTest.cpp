#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/RefCounted/New.h>
#include <BECore/Resource/TextureLibrary.h>
#include <BECore/SpriteLibraryTestModel.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Events/SubscriptionHolder.h>
#include <Generated/SpriteLibraryTestModel.gen.hpp>
#include <Widgets/ArtGen/ArtGenDrag.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <Widgets/TextureEditor/SpriteAtlasPacker.h>
#include <Widgets/TextureEditor/SpriteAtlasPanel.h>
#include <Widgets/TextureEditor/SpriteSheetGrid.h>
#include <Widgets/TextureEditor/SpriteSheetIslands.h>
#include <Widgets/TextureEditor/SpriteSheetSlicePanel.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 8;
        constexpr const char* kWindowName = "Sprite Library###SpriteLibrary";
        constexpr StringView kPanelConfigName = "SpriteLibraryPanel";
        constexpr StringView kShippedModel = "model=\"SpriteLibraryPanelModel\"";
        constexpr StringView kTestModel = "model=\"SpriteLibraryTestModel\"";
        constexpr int kSheetWidth = 60;
        constexpr int kSheetHeight = 20;

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "SpriteLibraryPanel.xml";
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        SpriteEntry EntryOf(StringView name, StringView path) {
            SpriteEntry entry;
            entry.name = PoolString::Intern(name);
            entry.texturePath = PoolString::Intern(path);
            entry.srcRect = Rect(0, 0, kSheetWidth, kSheetHeight);
            return entry;
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

        /// The commands the panel's buttons emitted
        class CommandRecorder : public SubscriptionHolder {
        public:
            CommandRecorder() {
                Subscribe<EditorCommandEvent, &CommandRecorder::OnCommand>(this);
            }

            [[nodiscard]] size_t Count(PoolString id) const {
                size_t count = 0;
                for (const PoolString seen : _seen) {
                    count += seen == id ? 1 : 0;
                }
                return count;
            }

        private:
            void OnCommand(const EditorCommandEvent& event) {
                _seen.push_back(event.commandId);
            }

            Vector<PoolString> _seen;
        };

    }  // namespace

    /// The shipped `SpriteLibraryPanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, over a library of its own, driven by
    /// the labels of `ImGuiItemIndex`. No texture loads here, so the slice section is given the sheet the preview would have noted (`UseSheet`)
    class SpriteLibraryAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("sprite_library_asset");
            MakeEmptyDirectory(_dir);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "SpriteLibraryPanel.xml", asset);
            ASSERT_TRUE(_fileSystem.Mount(MountPoints::config, _dir));
            _manager.Initialize();
            _library.AddOrUpdate(EntryOf("queen", "assets/queen.png"));
            _library.AddOrUpdate(EntryOf("rook", "assets/rook.png"));
            SpriteLibraryTestModel::library = &_library;

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
            ASSERT_NE(SpriteLibraryTestModel::instance, nullptr);
        }

        void TearDown() override {
            _widget.Reset();
            _index.Disarm();
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
            SpriteLibraryTestModel::library = nullptr;
            RemoveDirectory(_dir);
        }

        static SpriteLibraryTestModel& Model() {
            return *SpriteLibraryTestModel::instance;
        }

        static const SpriteLibraryPanelView& View() {
            return Model().GetView();
        }

        /// The window fills the display, so no control of the right pane is clipped away
        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetWindowPos(kWindowName, ImVec2(0.0f, 0.0f));
            ImGui::SetWindowSize(kWindowName, kDisplaySize);
            if (!_sheet.Empty()) {
                Model().NoteSheet(_sheet, kSheetWidth, kSheetHeight);
            }
            _widget->Update();
            ImGui::Render();
        }

        void Settle() {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        }

        void Select(StringView name) {
            Model().SelectByName(PoolString::Intern(name));
            Settle();
        }

        /// Selects @p name and has its texture noted as loaded at the start of every frame, as the preview does with a real texture
        void UseSheet(StringView name) {
            Select(name);
            _sheet = Model().GetSelected() != nullptr ? Model().GetSelected()->texturePath : PoolString{};
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

        /// The first visible item labelled exactly @p label after the item whose label holds @p after: a button inside a control
        const ImGuiItemRecord* FindAfter(StringView after, StringView label) {
            bool seen = false;
            for (const ImGuiItemRecord& item : _index.Items()) {
                seen = seen || (item.visible && Contains(item.label, after));
                if (seen && item.visible && StringView(item.label) == label) {
                    return &item;
                }
            }
            return nullptr;
        }

        /// The texture combo: ImGui gives the index no label for a closed combo, so it is the item drawn just before Rescan
        const ImGuiItemRecord* TextureCombo() {
            const ImGuiItemRecord* previous = nullptr;
            for (const ImGuiItemRecord& item : _index.Items()) {
                if (item.visible && Contains(item.label, "##rescan")) {
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

        void Press(ImGuiKey key) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(key, true);
            Frame();
            io.AddKeyEvent(key, false);
            Settle();
        }

        /// Clicks into the input, types @p text after what it holds and confirms with Enter
        void EditInput(StringView part, const char* text) {
            ASSERT_NO_FATAL_FAILURE(Click(part));
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
            Press(ImGuiKey_Enter);
        }

        void SetPacked(StringView name, bool packed) {
            const SpriteEntry* found = _library.GetSprite(PoolString::Intern(name));
            ASSERT_NE(found, nullptr);
            SpriteEntry entry = *found;
            SpriteAtlasLink link;
            link.name = "pieces"_intern;
            if (packed) {
                link.source = SpriteSource{};
                link.source->texturePath = entry.texturePath;
            }
            entry.atlas = link;
            _library.AddOrUpdate(std::move(entry));
            Settle();
        }

        Path _dir;
        FileSystem _fileSystem;
        ConfigManager _manager{_fileSystem};
        TextureLibrary _library;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
        PoolString _sheet;
    };

    TEST_F(SpriteLibraryAssetTest, WithNoSelectionTheWindowHoldsTheListAndAddOnly) {
        EXPECT_NE(ImGui::FindWindowByName(kWindowName), nullptr);
        EXPECT_NE(Find(ICON_LC_CIRCLE_PLUS " Add"), nullptr);
        EXPECT_NE(Find(ICON_LC_SAVE " Save"), nullptr);
        EXPECT_EQ(Find("##name"), nullptr);
        EXPECT_EQ(Find("##rescan"), nullptr);
        EXPECT_EQ(Find(ICON_LC_TRASH_2 " Delete Entry"), nullptr);
    }

    TEST_F(SpriteLibraryAssetTest, AddAddsASpriteSelectsItAndShowsItsForm) {
        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_CIRCLE_PLUS " Add"));

        EXPECT_NE(_library.GetSprite("new_sprite"_intern), nullptr);
        EXPECT_EQ(View().name, "new_sprite");
        EXPECT_TRUE(View().dirty);
        EXPECT_NE(Find("##name"), nullptr);
        EXPECT_NE(Find(ICON_LC_TRASH_2 " Delete Entry"), nullptr);
    }

    TEST_F(SpriteLibraryAssetTest, TheNameFieldRenamesTheSpriteWhenTheEditIsConfirmed) {
        Select("rook");

        ASSERT_NO_FATAL_FAILURE(EditInput("##name", "x"));

        EXPECT_NE(_library.GetSprite("rookx"_intern), nullptr);
        EXPECT_EQ(_library.GetSprite("rook"_intern), nullptr);
        EXPECT_EQ(View().name, "rookx");
    }

    TEST_F(SpriteLibraryAssetTest, TheTextureComboOpensOnlyWhileTheEntryIsNotPacked) {
        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", true));
        Select("rook");
        ASSERT_NO_FATAL_FAILURE(ClickItem(TextureCombo(), "the texture combo"));
        EXPECT_EQ(Find("(none)"), nullptr) << "a packed entry's texture waits for Unpack";

        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", false));
        ASSERT_NO_FATAL_FAILURE(ClickItem(TextureCombo(), "the texture combo"));
        EXPECT_NE(Find("(none)"), nullptr);
    }

    TEST_F(SpriteLibraryAssetTest, TheSliceSectionReportsItsStateAndItsControlsDriveTheSummary) {
        UseSheet("rook");
        EXPECT_FALSE(View().slice.open);
        EXPECT_EQ(Find("##columns"), nullptr) << "the section starts closed";

        ASSERT_NO_FATAL_FAILURE(Click("###slice"));
        EXPECT_TRUE(View().slice.open);
        ASSERT_NE(Find("##columns"), nullptr);
        EXPECT_EQ(Find("##alphaThreshold"), nullptr);
        const String gridSummary = View().slice.summary;

        ASSERT_NO_FATAL_FAILURE(ClickItem(FindAfter("##columns", "+"), "the + of Columns"));
        EXPECT_EQ(View().slice.columns, 7);
        EXPECT_NE(View().slice.summary, gridSummary);

        ASSERT_NO_FATAL_FAILURE(Click("###islands"));
        EXPECT_EQ(View().slice.mode, SheetSliceMode::Islands);
        EXPECT_EQ(Find("##columns"), nullptr);
        EXPECT_NE(Find("##alphaThreshold"), nullptr);
        EXPECT_NE(Find(ICON_LC_SCAN_SEARCH " Find islands"), nullptr);
    }

    /// The asset repeats the ranges and steps of the old form, which `Sync` clamps to: a field the asset lets go past them would jump back
    TEST_F(SpriteLibraryAssetTest, TheNumberFieldsHaveTheRangesAndStepsOfTheCode) {
        struct Expected {
            StringView id;
            int min;
            int max;
            int step;
            int stepFast;
        };
        using Slice = SpriteSheetSlicePanel;
        const Expected expected[] = {
            {"columns", 1, kMaxSheetGridAxis, Slice::kCountStep, Slice::kCountStepFast},
            {"rows", 1, kMaxSheetGridAxis, Slice::kCountStep, Slice::kCountStepFast},
            {"margin", 0, Slice::kMaxInsetPixels, Slice::kPixelStep, Slice::kPixelStepFast},
            {"spacing", 0, Slice::kMaxInsetPixels, Slice::kPixelStep, Slice::kPixelStepFast},
            {"alphaThreshold", 0, kMaxIslandAlphaThreshold, Slice::kAlphaStep, Slice::kAlphaStepFast},
            {"minSize", 1, Slice::kMaxIslandMinSize, Slice::kPixelStep, Slice::kPixelStepFast},
            {"padding", 0, kMaxIslandPadding, Slice::kPixelStep, Slice::kPixelStepFast},
            {"mergeGap", 0, kMaxIslandMergeGap, Slice::kPixelStep, Slice::kPixelStepFast},
            {"startIndex", 0, Slice::kMaxStartIndex, Slice::kCountStep, Slice::kCountStepFast},
            {"atlasPadding", 0, kMaxAtlasPadding, SpriteAtlasPanel::kPaddingStep, SpriteAtlasPanel::kPaddingStepFast},
        };
        const PanelLoadResult loaded = LoadPanelAsset(ReadFile(ShippedAssetFile()), PanelValidationContext{});
        ASSERT_TRUE(loaded.asset);
        for (const Expected& field : expected) {
            const PanelNumber* number = FindNumber(loaded.asset->elements, field.id);
            ASSERT_NE(number, nullptr) << String(field.id).c_str();
            EXPECT_TRUE(number->stepper) << String(field.id).c_str();
            EXPECT_EQ(number->min, static_cast<float>(field.min)) << String(field.id).c_str();
            EXPECT_EQ(number->max, static_cast<float>(field.max)) << String(field.id).c_str();
            EXPECT_EQ(number->step, static_cast<float>(field.step)) << String(field.id).c_str();
            EXPECT_EQ(number->stepFast, static_cast<float>(field.stepFast)) << String(field.id).c_str();
        }
    }

    TEST_F(SpriteLibraryAssetTest, TheAtlasRadiosWriteTheSizeMode) {
        Select("rook");
        ASSERT_NO_FATAL_FAILURE(Click("###atlas"));
        EXPECT_EQ(View().atlas.sizeMode, AtlasSizeMode::PowerOfTwo);

        ASSERT_NO_FATAL_FAILURE(Click("###tight"));
        EXPECT_EQ(View().atlas.sizeMode, AtlasSizeMode::Tight);
        ASSERT_NO_FATAL_FAILURE(Click("###powerOfTwo"));
        EXPECT_EQ(View().atlas.sizeMode, AtlasSizeMode::PowerOfTwo);
    }

    /// The model shows `Plan` while nothing is packed whatever the click, so the choice is seen once a member is packed again
    TEST_F(SpriteLibraryAssetTest, ThePreviewsCurrentIsOffWhileNothingIsPacked) {
        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", true));
        Select("rook");
        ASSERT_NO_FATAL_FAILURE(Click("###atlasPreview"));
        ASSERT_NO_FATAL_FAILURE(Click("###plan"));
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Plan);

        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", false));
        ASSERT_NO_FATAL_FAILURE(Click("###current"));
        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", true));
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Plan) << "the click on the disabled Current changed the choice";

        ASSERT_NO_FATAL_FAILURE(Click("###current"));
        EXPECT_EQ(View().preview.mode, AtlasPreviewMode::Current);
    }

    TEST_F(SpriteLibraryAssetTest, TheDropDialogOpensFromTheModelAndAcceptIsOffForARefusedName) {
        const CommandRecorder commands;
        SpriteLibraryDrop drop;
        drop.row = -1;
        drop.payload = MakeArtGenDragPayload(ArtGenDragKind::Image, 1, {}, {}, "knight");
        Model().NoteDrop(drop);
        Settle();
        ASSERT_TRUE(View().dropOpen);
        ASSERT_NE(Find("##dropName"), nullptr);

        // The focused field holds the suggested name selected, so typing replaces it
        ImGui::GetIO().AddInputCharactersUTF8("bad name");
        Settle();
        ASSERT_EQ(View().dropName, "bad name");
        ASSERT_TRUE(View().dropRefused);
        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_CHECK " Accept"));
        EXPECT_EQ(commands.Count(EditorCommandIds::kSpriteLibraryDropAccept), 0u);
        EXPECT_TRUE(View().dropOpen);

        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_X " Cancel"));
        EXPECT_FALSE(View().dropOpen);
        EXPECT_EQ(Find("##dropName"), nullptr);
    }

    TEST_F(SpriteLibraryAssetTest, ASteadyFrameWithTheSectionsOpenAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(SetPacked("rook", false));
        UseSheet("rook");
        ASSERT_NO_FATAL_FAILURE(Click("###slice"));
        ASSERT_NO_FATAL_FAILURE(Click("###atlas"));
        ASSERT_NO_FATAL_FAILURE(Click("###atlasPreview"));
        ASSERT_NE(Find("##columns"), nullptr);
        ASSERT_NE(Find("##atlasPadding"), nullptr);

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
