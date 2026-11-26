// The card of a component in the inspector (SceneEditor/ComponentCard) and its menu, in an ImGui context with no backend.

#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Events/SubscriptionHolder.h>
#include <Widgets/Feedback/ToastEvents.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Feedback/UIMotion.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentCard.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr ImVec2 kInspectorSize{600.0f, 880.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr uint64_t kNode = 7;
        constexpr StringView kHeader = "TransformComponent##card";
        constexpr StringView kMenuButton = "##card_menu";
        constexpr StringView kBodyButton = "Body";

        class ToastRecorder : public SubscriptionHolder {
        public:
            ToastRecorder() {
                Subscribe<EditorToastEvent, &ToastRecorder::OnToast>(this);
            }

            void OnToast(const EditorToastEvent& event) {
                levels.push_back(event.level);
            }

            Vector<ToastLevel> levels;
        };

        class ComponentCardTest : public ::testing::Test {
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
                _highlights.Connect(_service.Touches());
            }

            void TearDown() override {
                _index.Disarm();
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            /// One frame with the card of the transform, whose body is a button
            void Frame() {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kInspectorSize);
                ImGui::Begin("Inspector");
                const ComponentCardAnswer answer = DrawComponentCard(0, *_transform, {_defaults, _highlights, kNode}, [] { return ImGui::Button(kBodyButton.data()); });
                _changed |= answer.changed;
                _removed |= answer.remove;
                ImGui::End();
                ImGui::Render();
            }

            void Settle() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    Frame();
                }
            }

            /// The visible item whose label holds @p part; null when there is none
            [[nodiscard]] const ImGuiItemRecord* Find(StringView part) const {
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                        return &item;
                    }
                }
                return nullptr;
            }

            void Click(StringView part) {
                Settle();
                const ImGuiItemRecord* const item = Find(part);
                ASSERT_NE(item, nullptr) << "no item " << String(part).c_str();
                const ImVec2 at{item->CenterX(), item->CenterY()};
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(at.x, at.y);
                Frame();
                io.AddMouseButtonEvent(0, true);
                Frame();
                io.AddMouseButtonEvent(0, false);
                Settle();
            }

            void Choose(StringView menuItem) {
                Click(kMenuButton);
                Click(menuItem);
            }

            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
            ImGuiItemIndex _index;
            ControlService _service;
            IntrusivePtr<ControlClient> _agent = _service.OpenLocalClient("agent");
            TouchHighlights _highlights;
            ComponentDefaults _defaults;
            IntrusivePtr<TransformComponent> _transform = New<TransformComponent>();
            bool _changed = false;
            bool _removed = false;
        };

    }  // namespace

    TEST_F(ComponentCardTest, TheHeaderNamesTheTypeAfterItsIconAndTheMenuButtonStandsOnItsRow) {
        Settle();

        const ImGuiItemRecord& header = Require(Find(kHeader));
        const ImGuiItemRecord& menu = Require(Find(kMenuButton));

        EXPECT_GT(header.label.find(kHeader.data()), 1u) << "an icon and a space come first: " << header.label.c_str();
        EXPECT_FLOAT_EQ(menu.minY, header.minY);
        EXPECT_LE(menu.maxX, header.maxX);
        EXPECT_EQ(Find(kBodyButton), nullptr) << "a closed card draws no body";
    }

    TEST_F(ComponentCardTest, TheBodyOfAnOpenCardIsDrawnAndItsChangeIsAnswered) {
        Click(kHeader);

        Click(kBodyButton);

        EXPECT_TRUE(_changed);
        EXPECT_FALSE(_removed);
    }

    TEST_F(ComponentCardTest, ResetGivesTheDefaultsAndDropsTheMarksOfTheComponent) {
        _transform->_x = 12.0f;
        _service.ReportTouch(_agent.Get(), kNode, _transform->GetTypeMeta().typeName, "x");
        _highlights.Update(0.0f);

        Choose("Reset");

        EXPECT_FLOAT_EQ(_transform->_x, 0.0f);
        EXPECT_TRUE(_changed);
        EXPECT_EQ(_highlights.FindMark(kNode, _transform->GetTypeMeta().typeName, "x"), nullptr);
        EXPECT_EQ(Find("Reset"), nullptr) << "the menu closed";
    }

    TEST_F(ComponentCardTest, ResetIsDisabledWhileNothingDiffers) {
        Choose("Reset");

        EXPECT_FALSE(_changed);
        EXPECT_NE(Find("Reset"), nullptr) << "a disabled item leaves the menu open";
    }

    TEST_F(ComponentCardTest, CopyPutsTheElementOfASceneFileOnTheClipboard) {
        const IntrusivePtr<FakeClipboard> clipboard = New<FakeClipboard>();
        const ScopedClipboardBackend scope{IntrusivePtr<IClipboardBackend>(clipboard)};
        const ToastRecorder toasts;
        _transform->_x = 12.0f;

        Choose("Copy");

        EXPECT_EQ(clipboard->Content(), String(R"(<component type="TransformComponent" x="12" />)"));
        EXPECT_EQ(toasts.levels, (Vector<ToastLevel>{ToastLevel::Success}));
        EXPECT_FALSE(_changed);
    }

    TEST_F(ComponentCardTest, ACopyTheClipboardRefusesIsAnErrorToast) {
        const IntrusivePtr<FakeClipboard> clipboard = New<FakeClipboard>();
        clipboard->SetRefuse(true);
        const ScopedClipboardBackend scope{IntrusivePtr<IClipboardBackend>(clipboard)};
        const ToastRecorder toasts;

        Choose("Copy");

        EXPECT_EQ(toasts.levels, (Vector<ToastLevel>{ToastLevel::Error}));
    }

    TEST_F(ComponentCardTest, RemoveIsAnsweredToTheCaller) {
        Choose("Remove");

        EXPECT_TRUE(_removed);
        EXPECT_FALSE(_changed);
    }

    TEST_F(ComponentCardTest, ASettledFrameAllocatesNothing) {
        Click(kHeader);
        _index.Disarm();  // the index copies the labels
        Settle();

        ExpectNoAllocations([this] { Settle(); });
    }

}  // namespace BECore::Tests
