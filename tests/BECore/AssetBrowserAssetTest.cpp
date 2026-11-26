#include <BECore/AssetBrowserTestModel.h>
#include <BECore/Config/ConfigManager.h>
#include <BECore/FileSystem/MountPoints.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Resource/ResourceManager.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/RecordingRenderer.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Generated/AssetBrowserTestModel.gen.hpp>
#include <Widgets/AssetBrowser/AssetDrag.h>
#include <Widgets/EditorLayout/EditorCommands.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/AssetPanelWidget.h>
#include <gtest/gtest.h>
#include <imgui.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr int kSteadyFrames = 8;
        /// Longer than ImGui waits before a tooltip: its window is made by then
        constexpr int kTooltipFrames = 60;
        /// More tiles than the window shows at once
        constexpr int kManyFiles = 200;
        constexpr StringView kPanelConfigName = "AssetBrowserPanel";
        constexpr StringView kShippedModel = "model=\"AssetBrowserPanelModel\"";
        constexpr StringView kTestModel = "model=\"AssetBrowserTestModel\"";
        constexpr StringView kBoard = "assets/browser_probe_board.png";

        Path ShippedAssetFile() {
            return Path(__FILE__).Parent().Parent().Parent() / "config" / "panels" / "AssetBrowserPanel.xml";
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// A picture that needs no renderer and has a texture id of its own, so a test finds it in the draw data
        class ThumbnailProbe : public RecordedTexture {
        public:
            static constexpr uint32_t kSide = 8;

            ThumbnailProbe() : RecordedTexture(Vector<uint8_t>(static_cast<size_t>(kSide) * kSide * 4, 255), kSide, kSide) {}

            void* GetNativeHandle() const override {
                return const_cast<ThumbnailProbe*>(this);
            }
        };

        bool IsDrawn(const ITexture& texture) {
            const auto id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture.GetNativeHandle()));
            for (const ImDrawList* list : ImGui::GetDrawData()->CmdLists) {
                for (const ImDrawCmd& command : list->CmdBuffer) {
                    if (command.TexRef._TexData == nullptr && command.TexRef._TexID == id) {
                        return true;
                    }
                }
            }
            return false;
        }

    }  // namespace

    /// The shipped `AssetBrowserPanel.xml` hosted by a real `AssetPanelWidget` in an ImGui context with no backend, over a content root of its own, driven
    /// by the labels of `ImGuiItemIndex`. Textures are probes put into the resource cache; a picture with none there fails to load
    class AssetBrowserAssetTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            _dir = TestTempDirectory("asset_browser_asset");
            MakeEmptyDirectory(_dir);
            String asset = ReadFile(ShippedAssetFile());
            const size_t at = asset.find(kShippedModel.data(), 0, kShippedModel.size());
            ASSERT_NE(at, String::npos);
            asset.replace(at, kShippedModel.size(), kTestModel.data(), kTestModel.size());
            WriteFile(_dir / "AssetBrowserPanel.xml", asset);
            ASSERT_TRUE(_configFiles.Mount(MountPoints::config, _dir));
            _manager.Initialize();

            const Path content = _dir / "content";
            WriteFile(content / kBoard, "x");
            WriteFile(content / "assets/browser_probe_click.wav", "x");
            WriteFile(content / "assets/pieces/browser_probe_rook.wav", "x");
            _content.AddContentRoot(content);
            _content.Initialize();
            AssetBrowserTestModel::fileSystem = &_content;

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
            AssetBrowserTestModel::fileSystem = nullptr;
            for (const PoolString path : _cached) {
                CoreManager::GetResourceManager().Evict(path.ToStringView());
            }
            RemoveDirectory(_dir);
        }

        /// A texture the resource cache holds under @p path, so a load of it succeeds
        IntrusivePtr<ThumbnailProbe> CacheProbe(StringView path) {
            IntrusivePtr<ThumbnailProbe> probe = New<ThumbnailProbe>();
            const PoolString key = PoolString::Intern(path);
            // The manager has no way to hand it a resource, and the object behind the const view is not const
            const_cast<ResourceCache&>(CoreManager::GetResourceManager().GetCache()).Put(key, probe);
            _cached.push_back(key);
            return probe;
        }

        /// Makes the widget and shows its window, which the asset keeps hidden
        void Open() {
            _widget = New<AssetPanelWidget>();
            _widget->SetConfigManager(_manager);
            _widget->SetAsset(kPanelConfigName);
            const UniquePtr<IDeserializer> deserializer = _manager.CreateDeserializer(PoolString::Intern(kPanelConfigName));
            ASSERT_TRUE(deserializer);
            ASSERT_TRUE(_widget->Initialize(*deserializer));
            Toggle();
            ASSERT_NE(AssetBrowserTestModel::instance, nullptr);
        }

        void Toggle() {
            EditorCommandEvent::Emit(EditorCommandIds::kViewAssetBrowser);
            Settle();
        }

        static const AssetBrowserView& View() {
            return AssetBrowserTestModel::instance->GetView();
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

        /// A click on the centre of the item, which must be there
        void Click(StringView part) {
            const ImGuiItemRecord* item = Find(part);
            ASSERT_NE(item, nullptr) << String(part).c_str();
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(item->CenterX(), item->CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            Frame();
            Settle();
        }

        Path _dir;
        FileSystem _configFiles;
        ConfigManager _manager{_configFiles};
        FileSystem _content;
        Vector<PoolString> _cached;
        ImGuiFontBuffers _buffers;
        ImGuiFonts _fonts;
        ImGuiItemIndex _index;
        IntrusivePtr<AssetPanelWidget> _widget;
    };

    TEST_F(AssetBrowserAssetTest, TheCommandShowsTheHiddenWindowWithTheTreeAndTheFilesOfTheStartFolder) {
        const IntrusivePtr<ThumbnailProbe> board = CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());

        EXPECT_NE(Find("##search"), nullptr);
        EXPECT_NE(Find(" pieces"), nullptr);
        EXPECT_NE(Find("##browser_probe_board.png"), nullptr);
        EXPECT_NE(Find("##browser_probe_click.wav"), nullptr);
        EXPECT_EQ(Find("##browser_probe_rook.wav"), nullptr);
        EXPECT_TRUE(IsDrawn(*board));

        Toggle();
        EXPECT_EQ(Find("##search"), nullptr);
    }

    TEST_F(AssetBrowserAssetTest, AClickOnAFolderShowsItsFilesAndAClickOnATileSelectsIt) {
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());

        ASSERT_NO_FATAL_FAILURE(Click(" pieces"));
        EXPECT_EQ(View().folder, "assets/pieces");
        EXPECT_EQ(Find("##browser_probe_board.png"), nullptr);
        EXPECT_FALSE(View().hasSelection);

        ASSERT_NO_FATAL_FAILURE(Click("##browser_probe_rook.wav"));
        EXPECT_EQ(View().selectedPath, "assets/pieces/browser_probe_rook.wav");
        EXPECT_EQ(View().selectedKind, AssetKind::Sound);

        ASSERT_NO_FATAL_FAILURE(Click("All content"));
        EXPECT_EQ(View().folder, "");
        EXPECT_TRUE(View().showEmpty);
    }

    TEST_F(AssetBrowserAssetTest, ASearchTypedIntoTheToolbarThatMatchesNothingLeavesNoTile) {
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());

        ASSERT_NO_FATAL_FAILURE(Click("##search"));
        ImGui::GetIO().AddInputCharactersUTF8("rook");
        Settle();
        EXPECT_NE(Find("##browser_probe_rook.wav"), nullptr);
        EXPECT_EQ(Find("##browser_probe_click.wav"), nullptr);

        ImGui::GetIO().AddInputCharactersUTF8("zzz");
        Settle();
        EXPECT_TRUE(View().showEmpty);
        EXPECT_EQ(Find("##browser_probe_"), nullptr);
    }

    TEST_F(AssetBrowserAssetTest, ATextureThatDidNotLoadIsTriedAgainAfterRescanOnly) {
        ASSERT_NO_FATAL_FAILURE(Open());

        const IntrusivePtr<ThumbnailProbe> board = CacheProbe(kBoard);
        Settle();
        EXPECT_FALSE(IsDrawn(*board)) << "a failed load is remembered";

        ASSERT_NO_FATAL_FAILURE(Click(ICON_LC_REFRESH_CW " Rescan"));
        EXPECT_TRUE(IsDrawn(*board));
    }

    TEST_F(AssetBrowserAssetTest, OnlyTheTilesInViewLoadATextureAndClosingThePanelLetsThemGo) {
        Vector<IntrusivePtr<ThumbnailProbe>> probes;
        for (int i = 0; i < kManyFiles; ++i) {
            const String path = "assets/many/browser_probe_{}.png"_format(i);
            WriteFile(_dir / "content" / path, "x");
            probes.push_back(CacheProbe(path));
        }
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());
        // The cache and this case hold a probe; a thumbnail is the third holder
        const auto held = [&] {
            int count = 0;
            for (const IntrusivePtr<ThumbnailProbe>& probe : probes) {
                count += probe->GetRefCount() > 2 ? 1 : 0;
            }
            return count;
        };

        ASSERT_NO_FATAL_FAILURE(Click(" many"));
        EXPECT_GT(held(), 0);
        EXPECT_LT(held(), kManyFiles / 2);

        Toggle();
        EXPECT_EQ(held(), 0);
    }

    TEST_F(AssetBrowserAssetTest, ADragOfATileCarriesItsFile) {
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());
        const ImGuiItemRecord* tile = Find("##browser_probe_click.wav");
        ASSERT_NE(tile, nullptr);
        const ImVec2 from(tile->CenterX(), tile->CenterY());

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(from.x, from.y);
        Frame();
        io.AddMouseButtonEvent(0, true);
        Frame();
        io.AddMousePosEvent(kDisplaySize.x * 0.5f, kDisplaySize.y * 0.5f);
        Settle();

        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        ASSERT_NE(payload, nullptr);
        ASSERT_TRUE(payload->IsDataType(kAssetDragPayload));
        const auto& dragged = *static_cast<const AssetDragPayload*>(payload->Data);
        EXPECT_EQ(dragged.kind, AssetKind::Sound);
        EXPECT_STREQ(dragged.path, "assets/browser_probe_click.wav");
        io.AddMouseButtonEvent(0, false);
        Settle();
    }

    TEST_F(AssetBrowserAssetTest, TheDeleteButtonOpensTheQuestionAndEscClosesIt) {
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());
        ASSERT_NO_FATAL_FAILURE(Click("##browser_probe_click.wav"));
        ASSERT_TRUE(View().canChange) << View().deleteHint.c_str();

        ASSERT_NO_FATAL_FAILURE(Click(" Delete"));
        EXPECT_TRUE(View().change.deleteOpen);
        EXPECT_NE(Find(" Cancel"), nullptr);
        EXPECT_EQ(Find(" Delete anyway"), nullptr);

        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Escape, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Escape, false);
        Settle();

        EXPECT_FALSE(View().change.deleteOpen);
        EXPECT_EQ(Find(" Cancel"), nullptr);
        EXPECT_TRUE(IsFile(_dir / "content/assets/browser_probe_click.wav"));
    }

    TEST_F(AssetBrowserAssetTest, ASteadyFrameAllocatesNothing) {
        CacheProbe(kBoard);
        ASSERT_NO_FATAL_FAILURE(Open());
        ASSERT_NO_FATAL_FAILURE(Click("##browser_probe_board.png"));
        ASSERT_TRUE(View().hasSelection);
        // The pointer rests on the tile, so the measured frames draw its tooltip too
        for (int i = 0; i < kTooltipFrames; ++i) {
            Frame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
