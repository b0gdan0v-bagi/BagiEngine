// The tooltip of a component field (SceneEditor/FieldTooltips): its composed text, and the inspector showing it, in an ImGui context with no backend.

#include <BECore/ComponentDefaultsTest.h>
#include <BECore/FieldHelpTest.h>
#include <BECore/Input/ImGuiItemIndex.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/QuadRendererComponent.h>
#include <BECore/Scene/Components/SpriteRendererComponent.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/SceneEditor/ComponentDefaults.h>
#include <Widgets/SceneEditor/ComponentInspectors.h>
#include <Widgets/SceneEditor/FieldHelp.h>
#include <Widgets/SceneEditor/FieldTooltips.h>
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
        /// From the reset arrow of a difference line to a point of its text
        constexpr float kIntoTheLine = 20.0f;

        class FieldHelpTooltipTest : public ::testing::Test {
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
            }

            void TearDown() override {
                _index.Disarm();
                ImGui::DestroyContext();
                SetActiveImGuiFonts({});
            }

            /// One frame with the inspector of `_component` and a button after it that asks for the help of `flipX`; with @p log the text it drew goes into `_text`
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
                RenderComponentInspector(_component, nullptr, &_defaults, _withHelp ? &_tooltips : nullptr);
                ImGui::Button("Outside##outside");
                ShowFieldHelp("flipX");
                ImGui::Unindent();
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

            [[nodiscard]] const ImGuiItemRecord* Find(StringView part) const {
                for (const ImGuiItemRecord& item : _index.Items()) {
                    if (item.visible && StringView(item.label).find(part) != StringView::npos) {
                        return &item;
                    }
                }
                return nullptr;
            }

            /// Rests the pointer at the point until a tooltip would show and returns the text of that frame, the tooltip's included
            String TextAt(float x, float y) {
                ImGui::GetIO().AddMousePosEvent(x, y);
                for (int i = 0; i < kTooltipFrames; ++i) {
                    Frame();
                }
                Frame(true);
                return _text;
            }

            String TextOver(StringView part) {
                Settle();
                const ImGuiItemRecord& item = Require(Find(part));
                return TextAt(item.CenterX(), item.CenterY());
            }

            void Describe(StringView component, StringView field, StringView text) {
                _help.Set(FieldHelpFile::Engine, component, field, text);
            }

            static bool Contains(const String& text, StringView part) {
                return text.find(part.data(), 0, part.size()) != String::npos;
            }

            ImGuiFontBuffers _buffers;
            ImGuiFonts _fonts;
            ImGuiItemIndex _index;
            FieldHelp _help;
            ComponentDefaults _defaults;
            FieldTooltips _tooltips{_help, _defaults};
            IComponent* _component = nullptr;
            String _text;
            bool _withHelp = true;
        };

    }  // namespace

    TEST_F(FieldHelpTooltipTest, TheTextHoldsTheDescriptionTheTypeTheRangeAndTheDefault) {
        const IntrusivePtr<HelpProbeComponent> probe = New<HelpProbeComponent>();
        probe->speed = 7.0f;

        EXPECT_EQ(StringView(_tooltips.Of(*probe, "speed")), "How fast it goes\nfloat\nrange 0 .. 10\ndefault: 1");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "count")), "int32\ndefault: 3");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "no such field")), "");

        Describe("HelpProbeComponent", "count", "How many there are");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "count")), "How many there are\nint32\ndefault: 3") << "a change of the table is seen";
    }

    TEST_F(FieldHelpTooltipTest, TheTypeIsSaidInWordsAndAFieldThatIsNeverComparedHasNoDefault) {
        const IntrusivePtr<DefaultsProbeComponent> probe = New<DefaultsProbeComponent>();
        const IntrusivePtr<QuadRendererComponent> quad = New<QuadRendererComponent>();

        EXPECT_EQ(StringView(_tooltips.Of(*probe, "values")), "list of int32\ndefault: 1 items");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "limit")), "optional int32\ndefault: none");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "item")), "DefaultsProbeItem");
        EXPECT_EQ(StringView(_tooltips.Of(*probe, "holder")), "DefaultsProbeHolder");
        EXPECT_TRUE(StringView(_tooltips.Of(*quad, "color")).starts_with("Color\ndefault: ")) << _tooltips.Of(*quad, "color");
    }

    TEST_F(FieldHelpTooltipTest, HoveringANumberACheckboxOrATextShowsTheHelpOfTheField) {
        const IntrusivePtr<HelpProbeComponent> probe = New<HelpProbeComponent>();
        _component = probe.Get();
        Describe("HelpProbeComponent", "active", "Whether it runs");
        Describe("HelpProbeComponent", "title", "What it is called");
        Settle();
        Frame(true);
        EXPECT_FALSE(Contains(_text, "default:")) << "nothing is hovered: " << _text.c_str();

        // The generic inspector names an item by the index of its field
        for (const StringView item : {StringView("##0"), StringView("speed##label_0")}) {
            const String text = TextOver(item);
            EXPECT_TRUE(Contains(text, "How fast it goes")) << text.c_str();
            EXPECT_TRUE(Contains(text, "range 0 .. 10")) << text.c_str();
        }
        const String checkbox = TextOver("active##1");
        EXPECT_TRUE(Contains(checkbox, "Whether it runs")) << checkbox.c_str();
        EXPECT_TRUE(Contains(checkbox, "default: true")) << checkbox.c_str();
        const String field = TextOver("title##2");
        EXPECT_TRUE(Contains(field, "What it is called")) << field.c_str();
        EXPECT_TRUE(Contains(field, "default: probe")) << field.c_str();
    }

    TEST_F(FieldHelpTooltipTest, WithoutTooltipsAFieldShowsItsAttributeAlone) {
        const IntrusivePtr<HelpProbeComponent> probe = New<HelpProbeComponent>();
        _component = probe.Get();
        _withHelp = false;

        const String text = TextOver("##0");

        EXPECT_TRUE(Contains(text, "How fast it goes")) << text.c_str();
        EXPECT_FALSE(Contains(text, "default:")) << text.c_str();
    }

    TEST_F(FieldHelpTooltipTest, TheCustomInspectorsOfTheEngineShowTheHelpOfTheirFields) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        _component = sprite.Get();
        Describe("SpriteRendererComponent", "flipX", "Mirrored sideways");
        Describe("SpriteRendererComponent", "flipY", "Mirrored upside down");
        Describe("SpriteRendererComponent", "srcRect", "The part drawn");

        const String checkbox = TextOver("Flip X##");
        EXPECT_TRUE(Contains(checkbox, "Mirrored sideways")) << checkbox.c_str();
        for (const StringView item : {StringView("##srcRect_w"), StringView("w##label_srcRect_w")}) {
            const String text = TextOver(item);
            EXPECT_TRUE(Contains(text, "The part drawn")) << text.c_str();
        }

        const IntrusivePtr<ClickableComponent> clickable = New<ClickableComponent>();
        _component = clickable.Get();
        Describe("ClickableComponent", "blocksInput", "A shield");
        const String blocks = TextOver("Blocks input##");
        EXPECT_TRUE(Contains(blocks, "A shield")) << blocks.c_str();
    }

    TEST_F(FieldHelpTooltipTest, TheLineOfADifferingFieldOfACustomInspectorShowsItsHelp) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        _component = sprite.Get();
        sprite->_flipY = true;
        Describe("SpriteRendererComponent", "flipY", "Mirrored upside down");
        Settle();
        const ImGuiItemRecord& arrow = Require(Find("##reset_flipY"));

        const String text = TextAt(arrow.maxX + kIntoTheLine, arrow.CenterY());

        EXPECT_TRUE(Contains(text, "Mirrored upside down")) << text.c_str();
    }

    TEST_F(FieldHelpTooltipTest, ShowFieldHelpDoesNothingOutsideTheInspector) {
        const IntrusivePtr<SpriteRendererComponent> sprite = New<SpriteRendererComponent>();
        _component = sprite.Get();
        Describe("SpriteRendererComponent", "flipX", "Mirrored sideways");

        const String text = TextOver("Outside##");

        EXPECT_FALSE(Contains(text, "Mirrored sideways")) << text.c_str();
    }

    TEST_F(FieldHelpTooltipTest, ASettledFrameAllocatesNothingHoveredOrNot) {
        const IntrusivePtr<HelpProbeComponent> probe = New<HelpProbeComponent>();
        _component = probe.Get();
        const String hovered = TextOver("##0");
        ASSERT_TRUE(Contains(hovered, "How fast it goes")) << hovered.c_str();
        _index.Disarm();  // the index copies the labels
        Settle();

        ExpectNoAllocations([this] {
            Settle();
            ImGui::GetIO().AddMousePosEvent(-1.0f, -1.0f);
            Settle();
        });
    }

}  // namespace BECore::Tests
