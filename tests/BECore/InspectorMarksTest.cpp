// The marks of what a client of the control API changed, as the component inspector shows and drops them, in an ImGui context with no backend.

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Feedback/UIMotion.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentInspectors.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr ImVec2 kInspectorSize{600.0f, 880.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr int kSettleFrames = 3;
        constexpr float kDragDistance = 40.0f;
        constexpr uint64_t kNode = 7;
        constexpr StringView kMarkText = "changed:";

        size_t CountOf(StringView text, StringView part) {
            size_t count = 0;
            for (size_t at = text.find(part); at != StringView::npos; at = text.find(part, at + part.size())) {
                ++count;
            }
            return count;
        }

        class InspectorMarksTest : public ::testing::Test {
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

            /// A report of the agent about the inspected node, taken in at once
            void Touch(const IComponent& component, StringView field, uint64_t node = kNode) {
                _service.ReportTouch(_agent.Get(), node, component.GetTypeMeta().typeName, field);
                _highlights.Update(0.0f);
            }

            [[nodiscard]] const ControlClient* Mark(const IComponent& component, StringView field, uint64_t node = kNode) const {
                return _highlights.FindMark(node, component.GetTypeMeta().typeName, field);
            }

            /// One frame with the inspector of `_component`; with @p log the text it drew goes into `_text`
            void Frame(bool log = false) {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kInspectorSize);
                ImGui::Begin("Inspector");
                if (log) {
                    ImGui::LogBegin(ImGuiLogFlags_OutputBuffer, 0);
                }
                const ComponentTouches touches{_highlights, kNode, _component->GetTypeMeta().typeName};
                _changed |= RenderComponentInspector(_component, _marked ? &touches : nullptr);
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

            /// The text of a settled frame of the inspector
            StringView Drawn() {
                Settle();
                Frame(true);
                return _text;
            }

            /// The position among the visible items of the first whose label holds @p part
            [[nodiscard]] size_t IndexOf(StringView part) const {
                const auto& items = _index.Items();
                for (size_t i = 0; i < items.size(); ++i) {
                    if (items[i].visible && StringView(items[i].label).find(part) != StringView::npos) {
                        return i;
                    }
                }
                ADD_FAILURE() << "no item " << String(part).c_str();
                return 0;
            }

            [[nodiscard]] ImVec2 CenterOf(size_t index) const {
                const ImGuiItemRecord& item = At(_index.Items(), index);
                return {item.CenterX(), item.CenterY()};
            }

            /// Presses on @p at, moves the pointer by @p distance to the right and lets go
            void Drag(ImVec2 at, float distance) {
                ImGuiIO& io = ImGui::GetIO();
                io.AddMousePosEvent(at.x, at.y);
                Frame();
                io.AddMouseButtonEvent(0, true);
                Frame();
                io.AddMousePosEvent(at.x + distance, at.y);
                Settle();
                io.AddMouseButtonEvent(0, false);
                Settle();
            }

            void Click(StringView label) {
                Settle();
                Drag(CenterOf(IndexOf(label)), 0.0f);
            }

            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
            ImGuiItemIndex _index;
            ControlService _service;
            IntrusivePtr<ControlClient> _agent = _service.OpenLocalClient("agent");
            TouchHighlights _highlights;
            IComponent* _component = nullptr;
            String _text;
            bool _marked = true;
            bool _changed = false;
        };

    }  // namespace

    TEST_F(InspectorMarksTest, TheMarkStandsUnderItsFieldAndUnderNoOther) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        Settle();
        const float aboveBefore = CenterOf(IndexOf("clip##")).y;
        const float belowBefore = CenterOf(IndexOf("playOnStart##")).y;

        Touch(*audio, "volume");
        Touch(*audio, "bus", kNode + 1);  // the same component of another node
        const StringView drawn = Drawn();

        EXPECT_EQ(CountOf(drawn, kMarkText), 1u) << String(drawn).c_str();
        EXPECT_NE(drawn.find("changed: agent"), StringView::npos) << String(drawn).c_str();
        EXPECT_FLOAT_EQ(CenterOf(IndexOf("clip##")).y, aboveBefore);
        EXPECT_GT(CenterOf(IndexOf("playOnStart##")).y, belowBefore);
        EXPECT_FALSE(_changed);
    }

    TEST_F(InspectorMarksTest, AClientWithNoNameIsShownByItsNumber) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        const IntrusivePtr<ControlClient> unnamed = _service.OpenLocalClient("");
        _service.ReportTouch(unnamed.Get(), kNode, audio->GetTypeMeta().typeName, "volume");
        _highlights.Update(0.0f);

        const String expected = "changed: client {}"_format(unnamed->GetId());
        EXPECT_NE(Drawn().find(StringView(expected)), StringView::npos) << _text.c_str();
    }

    TEST_F(InspectorMarksTest, TheUsersEditOfAFieldDropsItsMarkAlone) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        Touch(*audio, "playOnStart");
        Touch(*audio, "volume");

        Click("playOnStart##");

        EXPECT_TRUE(_changed);
        EXPECT_EQ(Mark(*audio, "playOnStart"), nullptr);
        EXPECT_EQ(Mark(*audio, "volume"), _agent.Get());
        EXPECT_EQ(CountOf(Drawn(), kMarkText), 1u) << _text.c_str();
    }

    TEST_F(InspectorMarksTest, AChangeInsideACompoundMarksTheTopLevelFieldAndAnEditInsideDropsIt) {
        const IntrusivePtr<QuadRendererComponent> quad = New<QuadRendererComponent>();
        _component = quad.Get();
        Touch(*quad, "color");

        EXPECT_EQ(CountOf(Drawn(), kMarkText), 1u) << _text.c_str();
        Click("color##");  // opens the compound, which edits nothing
        EXPECT_EQ(Mark(*quad, "color"), _agent.Get());
        EXPECT_EQ(CountOf(Drawn(), kMarkText), 1u) << _text.c_str();

        // The first channel is the item after the node of the compound
        Drag(CenterOf(IndexOf("color##") + 1), -kDragDistance);

        EXPECT_TRUE(_changed);
        EXPECT_EQ(Mark(*quad, "color"), nullptr);
        EXPECT_EQ(CountOf(Drawn(), kMarkText), 0u) << _text.c_str();
    }

    TEST_F(InspectorMarksTest, ACustomInspectorListsItsMarksAndAnEditDropsThemAll) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = sprite.Get();
        Touch(*sprite, "spriteId");
        Touch(*sprite, "fit");
        Touch(*audio, "volume");

        const StringView drawn = Drawn();
        EXPECT_NE(drawn.find("spriteId changed: agent"), StringView::npos) << String(drawn).c_str();
        EXPECT_NE(drawn.find("fit changed: agent"), StringView::npos) << String(drawn).c_str();
        EXPECT_EQ(CountOf(drawn, kMarkText), 2u) << String(drawn).c_str();

        Click("Flip X##");

        EXPECT_TRUE(sprite->_flipX);
        EXPECT_EQ(Mark(*sprite, "spriteId"), nullptr);
        EXPECT_EQ(Mark(*sprite, "fit"), nullptr);
        EXPECT_EQ(Mark(*audio, "volume"), _agent.Get());
    }

    TEST_F(InspectorMarksTest, WithNoMarkTheInspectorDrawsAsWithNoTouches) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        _marked = false;
        Settle();
        const Vector<ImGuiItemRecord> unmarked(_index.Items().begin(), _index.Items().end());

        _marked = true;
        Touch(*audio, "volume", kNode + 1);
        Settle();

        ASSERT_EQ(_index.Items().size(), unmarked.size());
        for (size_t i = 0; i < unmarked.size(); ++i) {
            EXPECT_EQ(_index.Items()[i].label, unmarked[i].label) << i;
            EXPECT_FLOAT_EQ(_index.Items()[i].minY, unmarked[i].minY) << i;
        }
        EXPECT_EQ(CountOf(Drawn(), kMarkText), 0u) << _text.c_str();
    }

    TEST_F(InspectorMarksTest, ASteadyFrameAllocatesNothingWithOrWithoutAMark) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        _index.Disarm();  // the index copies the labels
        const auto frames = [this] {
            for (int i = 0; i < kSettleFrames; ++i) {
                Frame();
            }
        };
        Settle();
        ExpectNoAllocations(frames);

        Touch(*audio, "volume");
        _highlights.Update(kTouchFlashSeconds * 0.5f);
        Settle();

        ExpectNoAllocations(frames);
    }

}  // namespace BECore::Tests
