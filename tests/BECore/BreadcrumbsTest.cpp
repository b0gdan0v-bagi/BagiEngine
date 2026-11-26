// The path of the selected node above the inspector: links to the ancestors, wrapped to the window, in an ImGui context with no backend.

#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Scene/Scene.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/Breadcrumbs.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr float kWideWindow = 800.0f;
        /// Too narrow for the whole path, wide enough for any one of its names
        constexpr float kNarrowWindow = 130.0f;
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr StringView kCrumb = "##crumb";

        class BreadcrumbsTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.DisplaySize = kDisplaySize;
                io.IniFilename = nullptr;
                io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
                _fonts = LoadImGuiFonts(io, _buffers);
                SetActiveImGuiFonts(_fonts);
                ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
                _index.Arm(*ImGui::GetCurrentContext());

                _scene = New<Scene>();
                _scene->Initialize();
                _table = _scene->CreateNode("Table"_intern);
                _hand = _scene->CreateNode("Hand"_intern, _table.Get());
                _tile = _scene->CreateNode("Tile_12"_intern, _hand.Get());
                _selected = _tile.Get();
            }

            void TearDown() override {
                _index.Disarm();
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            /// One frame with the path of `_selected`; an ancestor it answers is kept in `_clicked`. With @p log the text it drew goes into `_text`
            void Frame(bool log = false) {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(ImVec2(_width, 400.0f));
                ImGui::Begin("Inspector");
                if (log) {
                    ImGui::LogBegin(ImGuiLogFlags_OutputBuffer, 0);
                }
                _right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                if (SceneNode* const clicked = DrawBreadcrumbs(*_selected, _ancestors)) {
                    _clicked.push_back(clicked);
                }
                if (log) {
                    _text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                    ImGui::LogFinish();
                }
                ImGui::End();
                ImGui::Render();
            }

            void Settle() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    Frame();
                }
            }

            [[nodiscard]] Vector<const ImGuiItemRecord*> Links() const {
                Vector<const ImGuiItemRecord*> links;
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (StringView(item.label).find(kCrumb) != StringView::npos) {
                        links.push_back(&item);
                    }
                }
                return links;
            }

            float _width = kWideWindow;
            float _right = 0.0f;
            IntrusivePtr<Scene> _scene;
            IntrusivePtr<SceneNode> _table;
            IntrusivePtr<SceneNode> _hand;
            IntrusivePtr<SceneNode> _tile;
            SceneNode* _selected = nullptr;
            Vector<SceneNode*> _ancestors;
            Vector<SceneNode*> _clicked;
            String _text;
            ImGuiItemIndex _index;
            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
        };

        TEST_F(BreadcrumbsTest, LinksEveryAncestorFromTheRootAndWritesTheNodePlain) {
            Settle();
            const Vector<const ImGuiItemRecord*> links = Links();
            ASSERT_EQ(links.size(), 3u);
            EXPECT_EQ(At(links, 0)->label, "Root##crumb0");
            EXPECT_EQ(At(links, 1)->label, "Table##crumb1");
            EXPECT_EQ(At(links, 2)->label, "Hand##crumb2");

            Frame(true);
            const size_t hand = _text.find("Hand");
            const size_t chevron = _text.find(ICON_LC_CHEVRON_RIGHT, hand);
            const size_t node = _text.find("Tile_12", hand);
            ASSERT_NE(node, String::npos) << _text.c_str();
            EXPECT_LT(chevron, node) << _text.c_str();
            EXPECT_EQ(_text.find("##"), String::npos) << _text.c_str();
        }

        TEST_F(BreadcrumbsTest, TheRootAloneHasNoLink) {
            _selected = &_scene->GetRootNode();
            Frame(true);
            EXPECT_TRUE(Links().empty());
            EXPECT_NE(_text.find("Root"), String::npos) << _text.c_str();
            EXPECT_EQ(_text.find(ICON_LC_CHEVRON_RIGHT), String::npos) << _text.c_str();
        }

        TEST_F(BreadcrumbsTest, AClickOnALinkAnswersItsNodeOnce) {
            Settle();
            const ImGuiItemRecord& table = *At(Links(), 1);
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(table.CenterX(), table.CenterY());
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            EXPECT_TRUE(_clicked.empty());
            io.AddMouseButtonEvent(0, false);
            Settle();
            ASSERT_EQ(_clicked.size(), 1u);
            EXPECT_EQ(_clicked.front(), _table.Get());
        }

        TEST_F(BreadcrumbsTest, WrapsAtTheRightEdgeOfANarrowWindow) {
            Settle();
            const float row = At(Links(), 0)->minY;
            for (const ImGuiItemRecord* const link : Links()) {
                EXPECT_EQ(link->minY, row) << link->label.c_str();
            }
            const float start = At(Links(), 0)->minX;

            _width = kNarrowWindow;
            Settle();
            const Vector<const ImGuiItemRecord*> links = Links();
            ASSERT_EQ(links.size(), 3u);
            EXPECT_GT(Back(links)->minY, At(links, 0)->minY);
            for (size_t i = 0; i < links.size(); ++i) {
                const ImGuiItemRecord& link = *links[i];
                EXPECT_LE(link.maxX, _right) << link.label.c_str();
                // The chevron wraps with its name, so no name but the first stands at the start of a row
                if (i > 0) {
                    EXPECT_GT(link.minX, start) << link.label.c_str();
                }
            }
        }

        TEST_F(BreadcrumbsTest, ASteadyFrameAllocatesNothing) {
            _index.Disarm();  // the index copies the labels
            _width = kNarrowWindow;
            Settle();

            ExpectNoAllocations([this] { Settle(); });
        }

    }  // namespace

}  // namespace BECore::Tests
