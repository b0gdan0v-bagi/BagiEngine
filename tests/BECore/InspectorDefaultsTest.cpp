// The marks and reset arrows of the fields that differ from their defaults, as the component inspector draws them, in an ImGui context with no backend.

#include "ComponentDefaultsTest.h"

#include <BECore/Audio/AudioSourceComponent.h>
#include <BECore/Control/ControlService.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Reflection/ClassMeta.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/Feedback/TouchHighlights.h>
#include <Widgets/Feedback/UIMotion.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
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
        /// Longer than the delay of a tooltip at kFrameSeconds a frame
        constexpr int kTooltipFrames = 60;
        constexpr uint64_t kNode = 7;
        constexpr StringView kArrow = "##reset_";

        class InspectorDefaultsTest : public ::testing::Test {
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

            /// One frame with the inspector of `_component`, indented as a card's body is; with @p log the text it drew goes into `_text`
            void Frame(bool log = false) {
                ImGui::GetIO().DeltaTime = kFrameSeconds;
                ImGui::NewFrame();
                ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
                ImGui::SetNextWindowSize(kInspectorSize);
                ImGui::Begin("Inspector");
                if (log) {
                    ImGui::LogBegin(ImGuiLogFlags_OutputBuffer, 0);
                }
                ImGui::Indent();
                ImGui::BeginDisabled(_disabled);
                const ComponentTouches touches{_highlights, kNode, _component->GetTypeMeta().typeName};
                _changed |= RenderComponentInspector(_component, &touches, _compared ? &_defaults : nullptr);
                ImGui::EndDisabled();
                ImGui::Unindent();
                _accentVertices = CountAccentVertices();
                if (log) {
                    _text = ImGui::GetCurrentContext()->LogBuffer.c_str();
                    ImGui::LogFinish();
                }
                ImGui::End();
                ImGui::Render();
            }

            /// The vertices of the window in the accent colour, whatever their alpha (a disabled scope dims it)
            static size_t CountAccentVertices() {
                const ImU32 accent = ImGui::GetColorU32(GetImGuiPalette().accent) & ~IM_COL32_A_MASK;
                size_t count = 0;
                for (const ImDrawVert& vertex : ImGui::GetWindowDrawList()->VtxBuffer) {
                    count += (vertex.col & ~IM_COL32_A_MASK) == accent ? 1u : 0u;
                }
                return count;
            }

            void Settle() {
                for (int i = 0; i < kSettleFrames; ++i) {
                    Frame();
                }
            }

            [[nodiscard]] const ImGuiItemRecord* Find(StringView part) const {
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                        return &item;
                    }
                }
                return nullptr;
            }

            [[nodiscard]] size_t Arrows() const {
                size_t count = 0;
                for (const ImGuiItemRecord& item : _index.Items()) {
                    count += StringView(item.label).find(kArrow) != StringView::npos ? 1u : 0u;
                }
                return count;
            }

            void Hover(StringView part) {
                Settle();
                const ImGuiItemRecord* const item = Find(part);
                ASSERT_NE(item, nullptr) << "no item " << String(part).c_str();
                ImGui::GetIO().AddMousePosEvent(item->CenterX(), item->CenterY());
                Frame();
            }

            void Click(StringView part) {
                Hover(part);
                ImGuiIO& io = ImGui::GetIO();
                io.AddMouseButtonEvent(0, true);
                Frame();
                io.AddMouseButtonEvent(0, false);
                Settle();
            }

            void Touch(const IComponent& component, StringView field) {
                _service.ReportTouch(_agent.Get(), kNode, component.GetTypeMeta().typeName, field);
                _highlights.Update(0.0f);
            }

            [[nodiscard]] const ControlClient* Mark(const IComponent& component, StringView field) const {
                return _highlights.FindMark(kNode, component.GetTypeMeta().typeName, field);
            }

            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
            ImGuiItemIndex _index;
            ControlService _service;
            IntrusivePtr<ControlClient> _agent = _service.OpenLocalClient("agent");
            TouchHighlights _highlights;
            ComponentDefaults _defaults;
            IComponent* _component = nullptr;
            String _text;
            size_t _accentVertices = 0;
            bool _compared = true;
            bool _disabled = false;
            bool _changed = false;
        };

    }  // namespace

    TEST_F(InspectorDefaultsTest, AComponentAtItsDefaultsDrawsAsOneThatIsNotCompared) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        _compared = false;
        Settle();
        const Vector<ImGuiItemRecord> plain(_index.Items().begin(), _index.Items().end());
        const size_t plainVertices = _accentVertices;

        _compared = true;
        Settle();

        ASSERT_EQ(_index.Items().size(), plain.size());
        for (size_t i = 0; i < plain.size(); ++i) {
            EXPECT_EQ(_index.Items()[i].label, plain[i].label) << i;
            EXPECT_FLOAT_EQ(_index.Items()[i].minY, plain[i].minY) << i;
        }
        EXPECT_EQ(_accentVertices, plainVertices);
    }

    TEST_F(InspectorDefaultsTest, TheArrowAppearsBesideTheFieldThatDiffersAndGoesWithTheDifference) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        Settle();
        // The third reflected field: a number's item is named by its index
        const float fieldLeft = Require(Find("##2")).minX;
        const float below = Require(Find("playOnStart##")).minY;

        audio->_volume = 0.5f;
        Settle();

        ASSERT_EQ(Arrows(), 1u);
        const ImGuiItemRecord& arrow = Require(Find("##reset_volume"));
        EXPECT_LE(arrow.maxX, fieldLeft);
        EXPECT_FLOAT_EQ(arrow.minY, Require(Find("##2")).minY);
        EXPECT_FLOAT_EQ(Require(Find("playOnStart##")).minY, below) << "the mark takes no room";

        audio->_volume = 1.0f;
        Settle();

        EXPECT_EQ(Arrows(), 0u);
    }

    TEST_F(InspectorDefaultsTest, AClickOfTheArrowResetsItsFieldAloneAndDropsItsMark) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        audio->_volume = 0.5f;
        audio->_playOnStart = true;
        Touch(*audio, "volume");
        Touch(*audio, "playOnStart");

        Click("##reset_volume");

        EXPECT_FLOAT_EQ(audio->_volume, 1.0f);
        EXPECT_TRUE(audio->_playOnStart);
        EXPECT_TRUE(_changed);
        EXPECT_EQ(Mark(*audio, "volume"), nullptr);
        EXPECT_EQ(Mark(*audio, "playOnStart"), _agent.Get());
        EXPECT_EQ(Arrows(), 1u);
    }

    TEST_F(InspectorDefaultsTest, TheArrowBesideTheLastFieldOfAWindowWorksAsAnyOther) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        audio->_playAction = "jump"_intern;

        Click("##reset_playAction");

        EXPECT_TRUE(audio->_playAction.Empty());
        EXPECT_EQ(Arrows(), 0u);
    }

    TEST_F(InspectorDefaultsTest, TheTooltipOfTheArrowNamesTheDefault) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        audio->_volume = 0.5f;

        Hover("##reset_volume");
        for (int i = 0; i < kTooltipFrames; ++i) {
            Frame();
        }
        Frame(true);

        EXPECT_NE(_text.find("Reset to 1"), String::npos) << _text.c_str();
        EXPECT_FALSE(_changed);
    }

    TEST_F(InspectorDefaultsTest, ACustomInspectorListsItsDifferencesWithAnArrowEach) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        _component = sprite.Get();
        sprite->_flipX = true;
        sprite->_flipY = true;
        Settle();
        Frame(true);

        EXPECT_NE(_text.find("flipX differs from the default"), String::npos) << _text.c_str();
        EXPECT_NE(_text.find("flipY differs from the default"), String::npos) << _text.c_str();
        EXPECT_EQ(Arrows(), 2u);
        EXPECT_LT(Require(Find("##reset_flipX")).minY, Require(Find("Flip X##")).minY);

        Click("##reset_flipX");

        EXPECT_FALSE(sprite->_flipX);
        EXPECT_TRUE(sprite->_flipY);
        EXPECT_TRUE(_changed);
        EXPECT_EQ(Arrows(), 1u);
    }

    TEST_F(InspectorDefaultsTest, AReadOnlyFieldIsMarkedAndHasNoArrow) {
        const IntrusivePtr<DefaultsProbeComponent> probe = New<DefaultsProbeComponent>();
        _component = probe.Get();
        Settle();
        const size_t unmarked = _accentVertices;

        probe->serial = 2;
        Settle();

        EXPECT_GT(_accentVertices, unmarked);
        EXPECT_EQ(Arrows(), 0u);
    }

    TEST_F(InspectorDefaultsTest, ADisabledInspectorIsMarkedAndHasNoArrow) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        _disabled = true;
        Settle();
        const size_t unmarked = _accentVertices;

        audio->_volume = 0.5f;
        Settle();

        EXPECT_GT(_accentVertices, unmarked);
        EXPECT_EQ(Arrows(), 0u);
    }

    TEST_F(InspectorDefaultsTest, ASteadyFrameWithADifferenceAllocatesNothing) {
        const IntrusivePtr<AudioSourceComponent> audio = New<AudioSourceComponent>();
        _component = audio.Get();
        audio->_volume = 0.5f;
        _index.Disarm();  // the index copies the labels
        Settle();

        ExpectNoAllocations([this] { Settle(); });
    }

}  // namespace BECore::Tests
