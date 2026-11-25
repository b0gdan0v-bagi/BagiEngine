#include <BECore/Input/ImGuiItemIndex.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{800.0f, 600.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        /// The first frames of a window are hidden or settle its size; the lists are read after these
        constexpr int kSettleFrames = 2;
        constexpr ImVec2 kPanelPos{40.0f, 30.0f};
        constexpr ImVec2 kSmallPanelSize{240.0f, 90.0f};
        constexpr int kManyItems = 30;

        /// The rects and ids ImGui itself reports right after each widget: what the index must agree with
        struct Reported {
            ImGuiID id = 0;
            ImVec2 min;
            ImVec2 max;
        };

        Reported ReportLastItem() {
            return {ImGui::GetItemID(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        }

        class ImGuiItemIndexTest : public ::testing::Test {
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
                io.Fonts->AddFontDefault();
            }

            void TearDown() override {
                index.Disarm();
                ImGui::DestroyContext();
            }

            template <typename Submit>
            void RunFrame(Submit&& submit) {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                submit();
                ImGui::Render();
            }

            /// A window with a button, a checkbox and a tree node, as it is after the settle frames
            void RunPanelFrame() {
                RunFrame([this] {
                    ImGui::SetNextWindowPos(kPanelPos);
                    ImGui::Begin("Panel", nullptr, ImGuiWindowFlags_NoSavedSettings);
                    ImGui::Button("Run##a");
                    button = ReportLastItem();
                    ImGui::Checkbox("Flag", &flag);
                    checkbox = ReportLastItem();
                    if (ImGui::TreeNode("Section###sec")) {
                        ImGui::TreePop();
                    }
                    tree = ReportLastItem();
                    ImGui::Button("##icon");
                    ImGui::End();
                });
            }

            void RunPanelFrames() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    RunPanelFrame();
                }
            }

            ImGuiItemIndex index;
            Reported button;
            Reported checkbox;
            Reported tree;
            bool flag = false;
        };

        const ImGuiItemRecord* OnlyMatch(const ImGuiItemIndex& index, StringView label) {
            Vector<const ImGuiItemRecord*> found;
            index.FindByLabel(label, found);
            return found.size() == 1 ? found[0] : nullptr;
        }

    }  // namespace

    TEST_F(ImGuiItemIndexTest, AnArmedFrameHoldsTheRectAndTheLabelOfEveryItem) {
        index.Arm(*ImGui::GetCurrentContext());
        RunPanelFrames();

        const ImGuiItemRecord* run = OnlyMatch(index, "Run");
        ASSERT_NE(run, nullptr);
        EXPECT_EQ(run->label, "Run##a");
        EXPECT_EQ(run->window, "Panel");
        EXPECT_EQ(run->id, button.id);
        EXPECT_FLOAT_EQ(run->minX, button.min.x);
        EXPECT_FLOAT_EQ(run->minY, button.min.y);
        EXPECT_FLOAT_EQ(run->maxX, button.max.x);
        EXPECT_FLOAT_EQ(run->maxY, button.max.y);
        EXPECT_TRUE(run->visible);
        EXPECT_GT(run->CenterX(), run->minX);
        EXPECT_LT(run->CenterX(), run->maxX);
        EXPECT_GT(run->CenterY(), run->minY);
        EXPECT_LT(run->CenterY(), run->maxY);

        const ImGuiItemRecord* flagBox = OnlyMatch(index, "Flag");
        ASSERT_NE(flagBox, nullptr);
        EXPECT_EQ(flagBox->id, checkbox.id);
        EXPECT_FLOAT_EQ(flagBox->maxX, checkbox.max.x);
    }

    TEST_F(ImGuiItemIndexTest, AnItemIsFoundByTheWholeLabelByItsVisiblePartAndByItsId) {
        index.Arm(*ImGui::GetCurrentContext());
        RunPanelFrames();

        EXPECT_NE(OnlyMatch(index, "Run##a"), nullptr);
        EXPECT_NE(OnlyMatch(index, "Run"), nullptr);
        // The part after `###` is the identity, not the text a person reads
        EXPECT_NE(OnlyMatch(index, "Section"), nullptr);
        EXPECT_NE(OnlyMatch(index, "Section###sec"), nullptr);
        EXPECT_EQ(OnlyMatch(index, "sec"), nullptr);
        EXPECT_EQ(OnlyMatch(index, "Ru"), nullptr);
        // A label with nothing before the `##` has no text to find it by
        EXPECT_EQ(OnlyMatch(index, ""), nullptr);

        const ImGuiItemRecord* byId = index.FindById(tree.id);
        ASSERT_NE(byId, nullptr);
        EXPECT_EQ(byId->label, "Section###sec");
        EXPECT_EQ(index.FindById(0xDEAD'BEEFU), nullptr);
    }

    TEST_F(ImGuiItemIndexTest, AWindowIsFoundByItsNameWithItsRect) {
        index.Arm(*ImGui::GetCurrentContext());
        RunPanelFrames();

        const ImGuiItemRecord* panel = OnlyMatch(index, "Panel");
        ASSERT_NE(panel, nullptr);
        const ImGuiWindow* window = ImGui::FindWindowByName("Panel");
        ASSERT_NE(window, nullptr);
        EXPECT_EQ(panel->id, window->ID);
        EXPECT_FLOAT_EQ(panel->minX, window->Pos.x);
        EXPECT_FLOAT_EQ(panel->minY, window->Pos.y);
        EXPECT_FLOAT_EQ(panel->maxX, window->Pos.x + window->Size.x);
        EXPECT_FLOAT_EQ(panel->maxY, window->Pos.y + window->Size.y);
        EXPECT_TRUE(panel->visible);

        // The window is in Windows() and its items are not
        bool inWindows = false;
        for (const ImGuiItemRecord& record : index.Windows()) {
            inWindows = inWindows || record.id == panel->id;
        }
        EXPECT_TRUE(inWindows);
        for (const ImGuiItemRecord& record : index.Items()) {
            EXPECT_NE(record.id, panel->id);
        }
    }

    TEST_F(ImGuiItemIndexTest, AFrameBeforeArmAndAFrameAfterDisarmRecordNothing) {
        RunPanelFrames();
        index.Arm(*ImGui::GetCurrentContext());
        EXPECT_TRUE(index.Items().empty());
        EXPECT_TRUE(index.Windows().empty());

        RunPanelFrame();
        EXPECT_FALSE(index.Items().empty());

        index.Disarm();
        EXPECT_FALSE(index.IsArmed());
        EXPECT_FALSE(ImGui::GetCurrentContext()->TestEngineHookItems);
        EXPECT_TRUE(index.Items().empty());
        RunPanelFrame();
        EXPECT_TRUE(index.Items().empty());
        EXPECT_TRUE(index.Windows().empty());
    }

    TEST_F(ImGuiItemIndexTest, AnItemBelowTheEdgeOfItsWindowIsNotVisible) {
        index.Arm(*ImGui::GetCurrentContext());
        for (int frame = 0; frame < kSettleFrames; ++frame) {
            RunFrame([] {
                ImGui::SetNextWindowPos(kPanelPos);
                ImGui::SetNextWindowSize(kSmallPanelSize);
                ImGui::Begin("Small", nullptr, ImGuiWindowFlags_NoSavedSettings);
                bool value = false;
                for (int i = 0; i < kManyItems; ++i) {
                    ImGui::Checkbox(Format("Item {}", i).c_str(), &value);
                }
                ImGui::End();
            });
        }

        const ImGuiItemRecord* first = OnlyMatch(index, "Item 0");
        const ImGuiItemRecord* last = OnlyMatch(index, "Item 29");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(last, nullptr);
        EXPECT_TRUE(first->visible);
        EXPECT_FALSE(last->visible);
        EXPECT_GT(last->minY, first->maxY);
    }

    TEST_F(ImGuiItemIndexTest, TheNextFrameReplacesTheListOfTheFrameBefore) {
        index.Arm(*ImGui::GetCurrentContext());
        EXPECT_EQ(index.Frame(), ImGuiItemIndex::kNoFrame);
        RunPanelFrames();
        ASSERT_NE(OnlyMatch(index, "Run"), nullptr);
        const int armedFrame = index.Frame();
        EXPECT_NE(armedFrame, ImGuiItemIndex::kNoFrame);

        RunFrame([] {
            ImGui::Begin("Other", nullptr, ImGuiWindowFlags_NoSavedSettings);
            ImGui::Button("Only");
            ImGui::End();
        });

        EXPECT_EQ(index.Frame(), armedFrame + 1);
        EXPECT_EQ(OnlyMatch(index, "Run"), nullptr);
        EXPECT_NE(OnlyMatch(index, "Only"), nullptr);
        EXPECT_EQ(OnlyMatch(index, "Panel"), nullptr);
    }

    TEST(ImGuiItemIndexLifetimeTest, ADestroyedContextLeavesTheIndexDisarmed) {
        EngineEnvironment::EnsureCoreServices();
        ImGuiItemIndex index;
        ImGuiContext* context = ImGui::CreateContext();
        index.Arm(*context);
        EXPECT_TRUE(index.IsArmed());

        ImGui::DestroyContext(context);

        EXPECT_FALSE(index.IsArmed());
        index.Disarm();
    }

}  // namespace BECore::Tests
