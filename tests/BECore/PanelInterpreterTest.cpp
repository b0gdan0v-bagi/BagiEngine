#include <BECore/PanelInterpreterProbe.h>
#include <BECore/Reflection/TypedRef.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelDataSources.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Generated/PanelInterpreterProbe.gen.hpp>
#include <Widgets/Icons/IconsLucide.h>
#include <Widgets/ImGuiFonts.h>
#include <Widgets/ImGuiTheme.h>
#include <Widgets/PanelAssets/PanelInterpreter.h>
#include <Widgets/PanelAssets/PanelSlots.h>
#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

namespace BECore::Tests {

    namespace {

        constexpr ImVec2 kDisplaySize{1280.0f, 900.0f};
        constexpr ImVec2 kHostSize{400.0f, 300.0f};
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        constexpr const char* kHostName = "##Host";
        /// The mouse is moved over the host window in steps of this many pixels when a control is looked for
        constexpr float kSearchStep = 6.0f;
        constexpr int kWarmUpFrames = 3;
        constexpr int kSteadyFrames = 10;
        constexpr size_t kManyRows = 100;

        InterpreterRow MakeRow(StringView name, PanelTextStyle style) {
            InterpreterRow row;
            row.name = name;
            row.style = style;
            return row;
        }

        constexpr const char* kSample = R"(
<PanelAsset id="Sample" title="Sample">
  <sources><source name="probe" model="Probe"/></sources>
  <elements>
    <element type="PanelText" id="title" bind="probe.title"/>
    <element type="PanelRow" id="bar">
      <elements>
        <element type="PanelButton" id="go" text="Go" icon="check" command="probe.go" enableWhen="probe.flag"/>
        <element type="PanelButton" id="plain" text="Plain" command="probe.plain"/>
      </elements>
    </element>
    <element type="PanelRow" id="extra" visible="!probe.hidden">
      <elements>
        <element type="PanelButton" id="more" text="More" command="probe.more"/>
      </elements>
    </element>
    <element type="PanelTable" id="rows" source="probe.rows" borders="true">
      <columns>
        <column type="Text" id="name" title="Name" field="name" styleField="style"/>
        <column type="Button" id="apply" title="Apply" icon="check" tooltip="Apply this" command="probe.apply" argument="name"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kSelectable = R"(
<PanelAsset id="Pick" title="Pick">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" header="false">
      <columns>
        <column type="Selectable" id="name" field="name" command="probe.pick" argument="name"/>
      </columns>
    </element>
    <element type="PanelTable" id="quiet" source="probe.rows" header="false">
      <columns>
        <column type="Selectable" id="name" field="name"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kMissing = R"(
<PanelAsset id="Missing" title="Missing">
  <elements>
    <element type="PanelText" id="text" bind="nobody.title"/>
    <element type="PanelField" id="field" bind="nobody.flag"/>
    <element type="PanelFields" id="fields" bind="nobody.all"/>
    <element type="PanelButton" id="button" text="Go" command="nobody.go" enableWhen="nobody.flag"/>
    <element type="PanelTable" id="table" source="nobody.rows">
      <columns><column type="Text" id="name" field="name"/></columns>
    </element>
    <element type="PanelSlot" id="slot" name="nobody"/>
    <element type="PanelInput" id="input" bind="probe.title"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kField = R"(
<PanelAsset id="Field" title="Field">
  <elements><element type="PanelField" id="flag" bind="probe.flag"/></elements>
</PanelAsset>)";

        constexpr const char* kFields = R"(
<PanelAsset id="Fields" title="Fields">
  <elements><element type="PanelFields" id="all" bind="probe.inner"/></elements>
</PanelAsset>)";

        constexpr const char* kSlot = R"(
<PanelAsset id="Slot" title="Slot">
  <elements><element type="PanelSlot" id="chart" name="chart"/></elements>
</PanelAsset>)";

        constexpr const char* kLimited = R"(
<PanelAsset id="Limited" title="Limited">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" header="false" maxRows="{}">
      <columns><column type="Text" id="name" field="name"/></columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kBig = R"(
<PanelAsset id="Big" title="Big">
  <elements>
    <element type="PanelText" id="title" bind="probe.title" wrap="true"/>
    <element type="PanelSeparator" id="sep" title="Rows"/>
    <element type="PanelTable" id="rows" source="probe.rows">
      <columns>
        <column type="Text" id="name" title="Name" field="name" styleField="style"/>
        <column type="Selectable" id="pick" title="Pick" field="name" command="probe.pick" argument="name"/>
        <column type="Button" id="apply" title="Apply" icon="check" command="probe.apply" argument="name"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

    }  // namespace

    /// The interpreter in an ImGui context of its own with no backend, driven by the mouse like a person; the host window is `##Host`
    class PanelInterpreterTest : public ::testing::Test {
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
            fonts = LoadImGuiFonts(io, buffers);
            SetActiveImGuiFonts(fonts);
            ApplyImGuiTheme(ImGui::GetStyle(), 1.0f);
            handle = sources.Register("probe", [this] { return TypedRef::Of(probe); });
            probe.rows = {MakeRow("alpha", PanelTextStyle::Normal), MakeRow("beta", PanelTextStyle::Error), MakeRow("gamma", PanelTextStyle::Success)};
        }

        void TearDown() override {
            handle.Reset();
            interpreter.Prepare(IntrusivePtr<PanelAsset>{});
            ImGui::DestroyContext();
            SetActiveImGuiFonts({});
        }

        void Load(StringView xml) {
            const PanelLoadResult loaded = LoadPanelAsset(xml);
            ASSERT_TRUE(loaded.asset);
            ASSERT_TRUE(loaded.issues.empty()) << loaded.issues[0].message.c_str();
            interpreter.Prepare(loaded.asset);
            Settle();
        }

        /// ImGui sizes a window's content from the frame before, so a height is read after two frames
        void Settle() {
            Frame();
            Frame();
        }

        void Frame() {
            ImGui::GetIO().DeltaTime = kFrameSeconds;
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
            ImGui::SetNextWindowSize(kHostSize);
            constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;
            ImGui::Begin(kHostName, nullptr, hostFlags);
            interpreter.Draw(sources, result, &slots);
            ImGui::End();
            ImGui::Render();
            if (result.clicked) {
                lastCommand = result.command;
                lastArgument = result.argument;
                ++clicks;
            }
        }

        static ImGuiID HostId() {
            // ImGuiWindow has no default constructor, so Require() cannot stand in for it
            const ImGuiWindow* host = ImGui::FindWindowByName(kHostName);
            EXPECT_NE(host, nullptr);
            return host != nullptr ? host->ID : 0;
        }

        /// The id a control gets from the chain of the element ids around it: each `PushID` of a text hashes it with the id on top
        static ImGuiID Chain(ImGuiID seed, std::initializer_list<const char*> texts) {
            for (const char* text : texts) {
                seed = ImHashStr(text, 0, seed);
            }
            return seed;
        }

        /// The id of a control inside the table @p tableElement: the table pushes its own id, then the row and the column id come
        static ImGuiID TableCellChain(const char* tableElement, int row, const char* columnId) {
            const ImGuiID table = Chain(HostId(), {tableElement, "table"});
            return Chain(ImHashData(&row, sizeof(row), table), {columnId});
        }

        static float ContentHeight() {
            return ImGui::FindWindowByName(kHostName)->ContentSize.y;
        }

        /// A point of the host window where the mouse is over the control @p id, found by moving it over a grid; none when no point is
        Optional<ImVec2> TryLocate(ImGuiID id, float fromY = 0.0f) {
            ImGuiIO& io = ImGui::GetIO();
            for (float y = fromY; y < kHostSize.y; y += kSearchStep) {
                for (float x = 0.0f; x < kHostSize.x; x += kSearchStep) {
                    io.AddMousePosEvent(x, y);
                    Frame();
                    Frame();
                    const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;
                    if (id == 0 ? hovered != 0 : hovered == id) {
                        return ImVec2(x, y);
                    }
                }
            }
            return std::nullopt;
        }

        ImVec2 Locate(ImGuiID id, float fromY = 0.0f) {
            const Optional<ImVec2> point = TryLocate(id, fromY);
            EXPECT_TRUE(point.has_value()) << "control not found on the screen";
            return point.value_or(ImVec2(0.0f, 0.0f));
        }

        /// A press and a release at @p point, one frame each; the click of a button is seen in the frame of the release
        void ClickAt(ImVec2 point) {
            ImGuiIO& io = ImGui::GetIO();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            io.AddMouseButtonEvent(0, false);
            clicks = 0;
            Frame();
        }

        ImGuiFontBuffers buffers;
        ImGuiFonts fonts;
        InterpreterProbe probe;
        PanelDataSources sources;
        PanelSourceHandle handle;
        PanelSlots slots;
        PanelInterpreter interpreter;
        PanelDrawResult result;
        String lastCommand;
        String lastArgument;
        int clicks = 0;
    };

    TEST_F(PanelInterpreterTest, TheSampleDrawsEveryElementAndClicksNothingByItself) {
        Load(kSample);
        EXPECT_GT(ContentHeight(), 0.0f);
        EXPECT_FALSE(result.clicked);

        probe.flag = true;
        Settle();
        EXPECT_EQ(clicks, 0);
    }

    TEST_F(PanelInterpreterTest, AConditionHidesAnElementAndItsChildren) {
        Load(kSample);
        const ImGuiID more = Chain(HostId(), {"extra", "more", "More"});
        const float shown = ContentHeight();
        EXPECT_TRUE(TryLocate(more).has_value());

        probe.hidden = true;
        Settle();
        EXPECT_LT(ContentHeight(), shown);
        EXPECT_FALSE(TryLocate(more).has_value());
    }

    TEST_F(PanelInterpreterTest, AButtonHasTheIdOfTheChainOfTheElementIdsAroundIt) {
        probe.flag = true;
        Load(kSample);

        const ImGuiID go = Chain(HostId(), {"bar", "go", ICON_LC_CHECK " Go"});
        const ImGuiID plain = Chain(HostId(), {"bar", "plain", "Plain"});
        ClickAt(Locate(go));
        EXPECT_EQ(lastCommand, "probe.go");
        ClickAt(Locate(plain));
        EXPECT_EQ(lastCommand, "probe.plain");
        EXPECT_TRUE(lastArgument.empty());
    }

    TEST_F(PanelInterpreterTest, ARowsButtonHasTheIdOfItsTableRowAndColumnAndClicksWithItsOwnRow) {
        Load(kSample);

        const int row = 1;
        const ImGuiID apply = Chain(TableCellChain("rows", row, "apply"), {ICON_LC_CHECK "##apply"});
        const ImVec2 point = Locate(apply);
        const size_t rowsBefore = probe.rows.size();
        ClickAt(point);

        EXPECT_EQ(lastCommand, "probe.apply");
        EXPECT_EQ(lastArgument, "beta");
        EXPECT_EQ(probe.rows.size(), rowsBefore) << "the walk must leave the list alone: the caller runs the command after it";
    }

    TEST_F(PanelInterpreterTest, TheClickIsInTheResultOfTheFrameAndNotRunDuringTheWalk) {
        Load(kSample);
        const ImGuiID apply = Chain(TableCellChain("rows", 0, "apply"), {ICON_LC_CHECK "##apply"});
        const ImVec2 point = Locate(apply);

        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(point.x, point.y);
        Frame();
        io.AddMouseButtonEvent(0, true);
        Frame();
        io.AddMouseButtonEvent(0, false);
        Frame();
        EXPECT_TRUE(result.clicked);
        EXPECT_EQ(result.argument, "alpha");

        // The next frame has no click: the result is cleared by Draw, not kept
        Frame();
        EXPECT_FALSE(result.clicked);
        EXPECT_TRUE(result.command.empty());
    }

    TEST_F(PanelInterpreterTest, ADisabledButtonDoesNotClick) {
        probe.flag = true;
        Load(kSample);
        const ImVec2 point = Locate(Chain(HostId(), {"bar", "go", ICON_LC_CHECK " Go"}));

        probe.flag = false;
        ClickAt(point);
        EXPECT_EQ(clicks, 0);
    }

    TEST_F(PanelInterpreterTest, ASelectableCellClicksOnlyWithACommand) {
        Load(kSelectable);

        // The label of a selectable is its text and a suffix, under the ids of the row and the column
        const ImGuiID pick = Chain(TableCellChain("rows", 2, "name"), {"gamma##select"});
        ClickAt(Locate(pick));
        EXPECT_EQ(lastCommand, "probe.pick");
        EXPECT_EQ(lastArgument, "gamma");

        const ImGuiID quiet = Chain(TableCellChain("quiet", 0, "name"), {"alpha##select"});
        ClickAt(Locate(quiet));
        EXPECT_EQ(clicks, 0);
    }

    TEST_F(PanelInterpreterTest, MaxRowsLimitsTheRowsDrawn) {
        probe.rows.resize(10);
        Load(Format(kLimited, 0));
        const float all = ContentHeight();
        Load(Format(kLimited, 3));
        const float limited = ContentHeight();
        EXPECT_LT(limited, all);
    }

    TEST_F(PanelInterpreterTest, AGoneSourceIsAQuestionMarkAndCrashesNothing) {
        handle.Reset();
        Load(kMissing);
        EXPECT_GT(ContentHeight(), 0.0f);
        EXPECT_FALSE(result.clicked);

        // A source that comes back is read again at once
        handle = sources.Register("probe", [this] { return TypedRef::Of(probe); });
        Frame();
    }

    TEST_F(PanelInterpreterTest, ASlotIsCalledByNameAndAnUnknownOneIsAQuestionMark) {
        Load(kSlot);
        const float unknown = ContentHeight();

        int calls = 0;
        const PanelSlotHandle slot = slots.Register("chart", [&calls] {
            ++calls;
            ImGui::Dummy(ImVec2(10.0f, 100.0f));
        });
        Settle();
        EXPECT_GE(calls, 1);
        EXPECT_GT(ContentHeight(), unknown);
    }

    TEST_F(PanelInterpreterTest, AFieldShowsTheOneFieldOfTheObjectAndAnEditChangesIt) {
        Load(kField);
        ASSERT_FALSE(probe.flag);

        ClickAt(Locate(0));
        EXPECT_TRUE(probe.flag);
    }

    TEST_F(PanelInterpreterTest, FieldsShowAnObjectOpenAndAnEditOfItsFieldChangesIt) {
        Load(kFields);
        ASSERT_FALSE(probe.inner.on);

        // The first item is the tree node of the object; the field is below it
        ClickAt(Locate(0, 2.0f * ImGui::GetFrameHeight()));
        EXPECT_TRUE(probe.inner.on);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAHundredRowTableAllocatesNothing) {
        probe.title = "a title that is long enough to want its own text scratch";
        probe.rows.clear();
        for (size_t i = 0; i < kManyRows; ++i) {
            probe.rows.push_back(MakeRow(Format("row {}", i), i % 2 == 0 ? PanelTextStyle::Success : PanelTextStyle::Error));
        }
        Load(kBig);
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
