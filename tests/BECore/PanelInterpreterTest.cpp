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
        /// A popup that opens with a focused text field grows ImGui's own buffers over its first frames
        constexpr int kInputWarmUpFrames = 10;
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

        constexpr float kWideButtonEm = 9.0f;
        constexpr const char* kLiteralButton = R"(
<PanelAsset id="Literal" title="Literal">
  <elements>
    <element type="PanelButton" id="record" text="Record" command="probe.record" argument="start now" width="9"/>
    <element type="PanelRow" id="line">
      <elements>
        <element type="PanelButton" id="iconic" text="Icon" icon="check" command="probe.icon" argument="icon arg"/>
      </elements>
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

        constexpr const char* kInput = R"(
<PanelAsset id="Input" title="Input">
  <elements>
    <element type="PanelInput" id="filter" bind="probe.filter" hint="Filter" icon="check"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kInputColumn = R"(
<PanelAsset id="Notes" title="Notes">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" header="false">
      <columns>
        <column type="Input" id="note" field="note" enableField="enabled" hint="note"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kWholeObject = R"(
<PanelAsset id="Whole" title="Whole">
  <elements><element type="PanelFields" id="all" bind="probe"/></elements>
</PanelAsset>)";

        constexpr const char* kScrolling = R"(
<PanelAsset id="Scrolling" title="Scrolling">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" scrollRows="{}">
      <columns>
        <column type="Button" id="act" icon="check" field="name" enableField="enabled" command="probe.act" argument="name"/>
        <column type="Input" id="note" field="note" hint="note"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kScrollingAndInput = R"(
<PanelAsset id="Mixed" title="Mixed">
  <elements>
    <element type="PanelInput" id="filter" bind="probe.filter" hint="Filter" icon="check"/>
    <element type="PanelTable" id="rows" source="probe.rows" scrollRows="{}">
      <columns>
        <column type="Button" id="act" icon="check" field="name" enableField="enabled" command="probe.act" argument="name"/>
        <column type="Input" id="note" field="note" hint="note"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kPanes = R"(
<PanelAsset id="Panes" title="Panes">
  <elements>
    <element type="PanelRow" id="panes">
      <elements>
        <element type="PanelChild" id="left" width="12" height="6" border="true">
          <elements><element type="PanelButton" id="a" text="A" command="probe.a"/></elements>
        </element>
        <element type="PanelChild" id="right" width="12" height="6">
          <elements><element type="PanelButton" id="b" text="B" command="probe.b"/></elements>
        </element>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kDialog = R"(
<PanelAsset id="Dialog" title="Dialog">
  <elements>
    <element type="PanelButton" id="outside" text="Outside" command="probe.outside"/>
    <element type="PanelRow" id="panes">
      <elements>
        <element type="PanelChild" id="pane" width="12" height="4">
          <elements><element type="PanelButton" id="inner" text="Inner" command="probe.inner"/></elements>
        </element>
      </elements>
    </element>
    <element type="PanelPopup" id="dialog" title="Sure?" open="{}">
      <elements>
        <element type="PanelText" id="question" text="Really?"/>
        <element type="PanelButton" id="yes" text="Yes" command="probe.yes"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kTabs = R"(
<PanelAsset id="Tabbed" title="Tabbed">
  <elements>
    <element type="PanelTabs" id="bar">
      <elements>
        <element type="PanelTab" id="first" title="First">
          <elements>
            <element type="PanelText" id="note" text="In the first"/>
            <element type="PanelButton" id="goA" text="GoA" command="probe.a"/>
          </elements>
        </element>
        <element type="PanelTab" id="second" title="Second">
          <elements><element type="PanelButton" id="goB" text="GoB" command="probe.b"/></elements>
        </element>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kTabOutsideABar = R"(
<PanelAsset id="Loose" title="Loose">
  <elements>
    <element type="PanelTab" id="loose" title="Loose"><elements/></element>
    <element type="PanelTabs" id="bar">
      <elements>
        <element type="PanelButton" id="stray" text="Stray" command="probe.stray"/>
        <element type="PanelTab" id="only" title="Only"><elements/></element>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kClosingDialog = R"(
<PanelAsset id="Closing" title="Closing">
  <elements>
    <element type="PanelPopup" id="dialog" title="Name" open="probe.dialog" closeCommand="probe.close">
      <elements>
        <element type="PanelInput" id="name" bind="probe.filter" focus="true" submitCommand="probe.submit" commitCommand="probe.commitName"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kCommits = R"(
<PanelAsset id="Commits" title="Commits">
  <elements>
    <element type="PanelNumber" id="gain" bind="probe.gain" min="0" max="1" step="0.01" slider="true" width="10" commitCommand="probe.commitGain"/>
    <element type="PanelInput" id="text" bind="probe.filter" commitCommand="probe.commitText"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kConditions = R"(
<PanelAsset id="Conditions" title="Conditions">
  <elements>
    <element type="PanelButton" id="act" text="Act" command="probe.act" enableWhen="probe.level > 0 and !probe.hidden"/>
    <element type="PanelRow" id="shown" visible="probe.flag and probe.level >= 2 or probe.mode == Error">
      <elements>
        <element type="PanelButton" id="inside" text="Inside" command="probe.inside"/>
      </elements>
    </element>
    <element type="PanelRow" id="broken" visible="{}">
      <elements>
        <element type="PanelButton" id="kept" text="Kept" command="probe.kept"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kControls = R"(
<PanelAsset id="Controls" title="Controls">
  <elements>
    <element type="PanelCheckbox" id="chk" bind="probe.flag" text="Flag"/>
    <element type="PanelCheckbox" id="tog" bind="probe.hidden" labelBind="probe.title" look="Toggle" style="Error"/>
    <element type="PanelCombo" id="mode" bind="probe.mode" width="10"/>
    <element type="PanelCombo" id="pick" bind="probe.pick" items="probe.options" width="10"/>
    <element type="PanelCombo" id="choice" bind="probe.choice" items="probe.rows" itemField="name" width="10"/>
    <element type="PanelNumber" id="level" bind="probe.level" text="Level" min="0" max="5" step="1" width="10"/>
    <element type="PanelNumber" id="gain" bind="probe.gain" min="0" max="1" step="0.01" slider="true" width="10"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kSelectedRows = R"(
<PanelAsset id="Selected" title="Selected">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" header="false">
      <columns>
        <column type="Selectable" id="name" field="name" selectedField="{}"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kLines = R"(
<PanelAsset id="Lines" title="Lines">
  <elements><element type="PanelText" id="state" bind="probe.filter" mono="true" maxLines="{}"/></elements>
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
            probe.options = {"alpha", "beta", "gamma"};
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

        /// Draws an asset whose file has issues, as the widget keeps the last asset it could build; the loader still gives the tree
        void LoadWithIssues(StringView xml) {
            const PanelLoadResult loaded = LoadPanelAsset(xml);
            ASSERT_TRUE(loaded.asset);
            ASSERT_FALSE(loaded.issues.empty());
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

        /// A point of the rectangle @p from to @p to where the mouse is over the control @p id, found by moving it over a grid; none when no point is
        Optional<ImVec2> TryLocateIn(ImGuiID id, ImVec2 from, ImVec2 to) {
            ImGuiIO& io = ImGui::GetIO();
            for (float y = from.y; y < to.y; y += kSearchStep) {
                for (float x = from.x; x < to.x; x += kSearchStep) {
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

        /// A point of the host window where the mouse is over the control @p id; none when no point is
        Optional<ImVec2> TryLocate(ImGuiID id, float fromY = 0.0f) {
            return TryLocateIn(id, ImVec2(0.0f, fromY), kHostSize);
        }

        ImVec2 Locate(ImGuiID id, float fromY = 0.0f) {
            const Optional<ImVec2> point = TryLocate(id, fromY);
            EXPECT_TRUE(point.has_value()) << "control not found on the screen";
            return point.value_or(ImVec2(0.0f, 0.0f));
        }

        /// A point over the control @p id inside the window @p window, which is not the host (a popup is placed on the display, not in the host)
        ImVec2 LocateInWindow(ImGuiID id, const ImGuiWindow& window) {
            const Optional<ImVec2> point = TryLocateIn(id, window.Pos, ImVec2(window.Pos.x + window.Size.x, window.Pos.y + window.Size.y));
            EXPECT_TRUE(point.has_value()) << "control not found in the window";
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

        /// A press at @p from, the mouse moved by @p dx in steps and released: a drag of the control under it
        void DragBy(ImVec2 from, float dx) {
            constexpr int kSteps = 10;
            ImGuiIO& io = ImGui::GetIO();
            // A second press inside the double click time turns a drag into a text input
            const int pauseFrames = static_cast<int>(io.MouseDoubleClickTime / kFrameSeconds) + 1;
            for (int i = 0; i < pauseFrames; ++i) {
                Frame();
            }
            io.AddMousePosEvent(from.x, from.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            Frame();
            for (int step = 1; step <= kSteps; ++step) {
                io.AddMousePosEvent(from.x + dx * static_cast<float>(step) / kSteps, from.y);
                Frame();
            }
            io.AddMouseButtonEvent(0, false);
            Settle();
        }

        void PressEscape() {
            ImGuiIO& io = ImGui::GetIO();
            io.AddKeyEvent(ImGuiKey_Escape, true);
            Frame();
            io.AddKeyEvent(ImGuiKey_Escape, false);
            Frame();
        }

        /// The popup of the combo that is open: ImGui names it `##Combo_<depth>`
        static const ImGuiWindow* ComboPopup() {
            const ImGuiWindow* popup = ImGui::FindWindowByName("##Combo_00");
            EXPECT_NE(popup, nullptr) << "no combo is open";
            return popup;
        }

        /// The id of the row @p index of a combo's popup: the popup pushes the row, the label comes last
        static ImGuiID ComboRowId(const ImGuiWindow& popup, int index, const char* label) {
            return Chain(ImHashData(&index, sizeof(index), popup.ID), {label});
        }

        /// Opens the combo @p id, clicks its row @p index and leaves it closed
        void PickFromCombo(ImGuiID id, int index, const char* label) {
            ClickAt(Locate(id));
            Settle();
            const ImGuiWindow* popup = ComboPopup();
            ASSERT_NE(popup, nullptr);
            ClickAt(LocateInWindow(ComboRowId(*popup, index, label), *popup));
            Settle();
            EXPECT_FALSE(AnyPopupOpen()) << "the combo stays open";
        }

        /// How many vertices the host has drawn so far
        static int HostVertices() {
            return ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer.Size;
        }

        /// Characters typed into the text field that is active
        void Type(const char* text) {
            ImGui::GetIO().AddInputCharactersUTF8(text);
            Settle();
        }

        /// The child window of the element @p elementId inside the host: ImGui names it `<parent>/##<label>_<hash>`, and a control in it is
        /// identified from that window's id, not from the host's chain
        static const ImGuiWindow* ChildWindow(const char* elementId) {
            const ImGuiWindow* host = ImGui::FindWindowByName(kHostName);
            const String marker = Format("##{}_", elementId);
            for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
                if (window->ParentWindow == host && std::strstr(window->Name, marker.c_str()) != nullptr) {
                    return window;
                }
            }
            ADD_FAILURE() << "no child window " << elementId;
            return host;
        }

        static bool AnyPopupOpen() {
            return !ImGui::GetCurrentContext()->OpenPopupStack.empty();
        }

        /// How many vertices of the host drawn so far have the colour of a selected row
        static size_t SelectedVertices() {
            const ImU32 selected = ImGui::GetColorU32(ImGuiCol_Header);
            size_t count = 0;
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                count += vertex.col == selected ? 1 : 0;
            }
            return count;
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

    TEST_F(PanelInterpreterTest, AButtonPassesItsLiteralArgumentAndHasTheWidthItWasGiven) {
        Load(kLiteralButton);

        ClickAt(Locate(Chain(HostId(), {"record", "Record"})));
        EXPECT_EQ(lastCommand, "probe.record");
        EXPECT_EQ(lastArgument, "start now");
        ClickAt(Locate(Chain(HostId(), {"line", "iconic", ICON_LC_CHECK " Icon"})));
        EXPECT_EQ(lastArgument, "icon arg");

        const float width = ImGui::FindWindowByName(kHostName)->ContentSize.x;
        EXPECT_NEAR(width, kWideButtonEm * ImGui::GetFontSize(), 1.0f);
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

    TEST_F(PanelInterpreterTest, AnInputHasTheIdOfItsElementAndTypingChangesTheBoundText) {
        probe.filter = "ab";
        Load(kInput);

        ClickAt(Locate(Chain(HostId(), {"filter", "##filter"})));
        Type("cd");
        EXPECT_EQ(probe.filter.size(), 4u);
        EXPECT_NE(probe.filter.find("cd"), String::npos);
    }

    TEST_F(PanelInterpreterTest, AnInputOverATextLongerThanItsBufferShowsACutAtACharacterAndKeepsTheTextValid) {
        // 'é' is two bytes and the buffer holds an odd number of them, so the cut falls inside a character
        probe.filter.clear();
        for (size_t i = 0; i < PanelInterpreter::kInputBufferSize; ++i) {
            probe.filter.append("\xC3\xA9");
        }
        Load(kInput);
        const String before = probe.filter;
        Settle();
        EXPECT_EQ(probe.filter, before) << "drawing alone must not write";

        ClickAt(Locate(Chain(HostId(), {"filter", "##filter"})));
        Type("x");
        ASSERT_NE(probe.filter.find('x'), String::npos);
        EXPECT_EQ(probe.filter.size(), PanelInterpreter::kInputBufferSize - 1);
        // Every 'é' that is left is whole: two bytes, the second a continuation
        size_t whole = 0;
        for (size_t i = 0; i + 1 < probe.filter.size(); ++i) {
            if (static_cast<uint8_t>(probe.filter[i]) == 0xC3) {
                EXPECT_EQ(static_cast<uint8_t>(probe.filter[i + 1]), 0xA9);
                ++whole;
            }
        }
        EXPECT_EQ(whole, (PanelInterpreter::kInputBufferSize - 2) / 2);
    }

    TEST_F(PanelInterpreterTest, AnInputColumnEditsItsOwnRowAndStaysOffWhenItsEnableFieldIsFalse) {
        probe.rows[2].enabled = false;
        Load(kInputColumn);

        ClickAt(Locate(Chain(TableCellChain("rows", 1, "note"), {"##note"})));
        Type("hi");
        EXPECT_EQ(probe.rows[1].note, "hi");
        EXPECT_TRUE(probe.rows[0].note.empty());
        EXPECT_TRUE(probe.rows[2].note.empty());

        ClickAt(Locate(Chain(TableCellChain("rows", 2, "note"), {"##note"})));
        Type("no");
        EXPECT_TRUE(probe.rows[2].note.empty());
    }

    TEST_F(PanelInterpreterTest, FieldsWithASourceAloneDrawTheWholeObjectAndAnEditChangesItsFirstField) {
        Load(kWholeObject);
        ASSERT_FALSE(probe.flag);

        ClickAt(Locate(0));
        EXPECT_TRUE(probe.flag);
    }

    TEST_F(PanelInterpreterTest, AScrollingTableDrawsOnlyTheRowsInView) {
        probe.rows.clear();
        for (size_t i = 0; i < kManyRows; ++i) {
            probe.rows.push_back(MakeRow(Format("row {}", i), PanelTextStyle::Normal));
        }
        Load(Format(kScrolling, 5));

        EXPECT_TRUE(TryLocate(Chain(TableCellChain("rows", 0, "act"), {ICON_LC_CHECK " row 0##act"})).has_value());
        EXPECT_FALSE(TryLocate(Chain(TableCellChain("rows", 50, "act"), {ICON_LC_CHECK " row 50##act"})).has_value());
        EXPECT_FALSE(TryLocate(Chain(TableCellChain("rows", kManyRows - 1, "act"), {ICON_LC_CHECK " row 99##act"})).has_value());
    }

    TEST_F(PanelInterpreterTest, AScrollRowsTableThatFitsLeavesNoRoomEmptyBelowItsRows) {
        Load(Format(kScrolling, 50));
        const float fits = ContentHeight();
        Load(Format(kScrolling, 0));
        EXPECT_FLOAT_EQ(fits, ContentHeight());
    }

    TEST_F(PanelInterpreterTest, ALabelledButtonCellClicksWithItsRowsArgumentAndANotEnabledRowDoesNot) {
        probe.rows[1].enabled = false;
        Load(Format(kScrolling, 5));

        ClickAt(Locate(Chain(TableCellChain("rows", 2, "act"), {ICON_LC_CHECK " gamma##act"})));
        EXPECT_EQ(lastCommand, "probe.act");
        EXPECT_EQ(lastArgument, "gamma");

        clicks = 0;
        ClickAt(Locate(Chain(TableCellChain("rows", 1, "act"), {ICON_LC_CHECK " beta##act"})));
        EXPECT_EQ(clicks, 0);
    }

    TEST_F(PanelInterpreterTest, ATextWithMaxLinesIsAScrollRegionOfThatHeight) {
        probe.filter.clear();
        for (int i = 0; i < 40; ++i) {
            probe.filter.append("a line of the state\n");
        }
        Load(Format(kLines, 0));
        const float all = ContentHeight();
        Load(Format(kLines, 3));
        const float limited = ContentHeight();
        EXPECT_GT(limited, 0.0f);
        EXPECT_LT(limited, all);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAScrollingTableWithAButtonAndAnInputColumnAndAnInputAllocatesNothing) {
        probe.rows.clear();
        for (size_t i = 0; i < kManyRows; ++i) {
            probe.rows.push_back(MakeRow(Format("row {}", i), PanelTextStyle::Normal));
            probe.rows.back().note = "a note";
        }
        probe.filter = "a filter long enough to want its own text scratch";
        Load(Format(kScrollingAndInput, 10));
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }

        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(PanelInterpreterTest, AChildDrawsItsChildrenAndTwoChildrenOfARowSitSideBySide) {
        Load(kPanes);
        const ImGuiWindow* left = ChildWindow("left");
        const ImGuiWindow* right = ChildWindow("right");
        EXPECT_GE(right->Pos.x, left->Pos.x + left->Size.x);
        EXPECT_FLOAT_EQ(right->Pos.y, left->Pos.y);

        ClickAt(Locate(Chain(left->ID, {"a", "A"})));
        EXPECT_EQ(lastCommand, "probe.a");
        ClickAt(Locate(Chain(right->ID, {"b", "B"})));
        EXPECT_EQ(lastCommand, "probe.b");
    }

    TEST_F(PanelInterpreterTest, APopupOpensWhenItsBoolTurnsTrueTakesTheClicksAndClosesWhenItTurnsFalse) {
        Load(Format(kDialog, "probe.dialog"));
        const ImVec2 outside = Locate(Chain(HostId(), {"outside", "Outside"}));
        EXPECT_FALSE(AnyPopupOpen());
        EXPECT_EQ(ImGui::FindWindowByName("Sure?##dialog"), nullptr);

        probe.dialog = true;
        Settle();
        ASSERT_TRUE(AnyPopupOpen());
        const ImGuiWindow* popup = ImGui::FindWindowByName("Sure?##dialog");
        ASSERT_NE(popup, nullptr);

        ClickAt(outside);
        EXPECT_EQ(clicks, 0) << "a modal takes every click of the window under it";

        ClickAt(LocateInWindow(Chain(popup->ID, {"yes", "Yes"}), *popup));
        EXPECT_EQ(lastCommand, "probe.yes");
        EXPECT_TRUE(lastArgument.empty());

        // The command of the click clears the bool; the popup is gone by the end of the next frame
        probe.dialog = false;
        Frame();
        EXPECT_FALSE(AnyPopupOpen());
        ClickAt(outside);
        EXPECT_EQ(lastCommand, "probe.outside");
    }

    TEST_F(PanelInterpreterTest, APopupOpensAgainWhenItsBoolTurnsTrueAgain) {
        Load(Format(kDialog, "probe.dialog"));
        for (int round = 0; round < 2; ++round) {
            probe.dialog = true;
            Settle();
            EXPECT_TRUE(AnyPopupOpen());
            probe.dialog = false;
            Settle();
            EXPECT_FALSE(AnyPopupOpen());
        }
    }

    TEST_F(PanelInterpreterTest, APopupWhoseConditionCannotBeReadStaysClosed) {
        Load(Format(kDialog, "nobody.dialog"));
        Settle();
        EXPECT_FALSE(AnyPopupOpen());

        Load(Format(kDialog, "probe.nothing"));
        Settle();
        EXPECT_FALSE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, AnExpressionOfAndOrAndACompareHidesAndShowsAnElementAndItsChildren) {
        Load(Format(kConditions, "probe.flag"));
        const ImGuiID inside = Chain(HostId(), {"shown", "inside", "Inside"});
        EXPECT_FALSE(TryLocate(inside).has_value());

        probe.flag = true;
        Settle();
        EXPECT_FALSE(TryLocate(inside).has_value()) << "and wants the compare too";

        probe.level = 2;
        Settle();
        EXPECT_TRUE(TryLocate(inside).has_value());

        probe.flag = false;
        Settle();
        EXPECT_FALSE(TryLocate(inside).has_value());

        probe.mode = PanelTextStyle::Error;
        Settle();
        EXPECT_TRUE(TryLocate(inside).has_value()) << "or takes the other alternative";
    }

    TEST_F(PanelInterpreterTest, AnExpressionDisablesAButtonWhileOneOfItsTermsIsFalse) {
        probe.level = 1;
        Load(Format(kConditions, "probe.flag"));
        const ImVec2 point = Locate(Chain(HostId(), {"act", "Act"}));
        ClickAt(point);
        EXPECT_EQ(lastCommand, "probe.act");

        lastCommand.clear();
        probe.hidden = true;
        ClickAt(point);
        EXPECT_TRUE(lastCommand.empty());

        probe.hidden = false;
        probe.level = 0;
        ClickAt(point);
        EXPECT_TRUE(lastCommand.empty());
    }

    TEST_F(PanelInterpreterTest, AConditionWithATermThatCannotBeReadOrThatDoesNotParseIsTrue) {
        for (const char* text : {"probe.nothing > 1", "nobody.flag", "!probe.flag and probe.nothing == Error"}) {
            Load(Format(kConditions, text));
            EXPECT_TRUE(TryLocate(Chain(HostId(), {"broken", "kept", "Kept"})).has_value()) << text;
        }
        LoadWithIssues(Format(kConditions, "and and"));
        EXPECT_TRUE(TryLocate(Chain(HostId(), {"broken", "kept", "Kept"})).has_value());
    }

    TEST_F(PanelInterpreterTest, APopupOpensOnlyWhenEveryTermOfItsExpressionReadsTrueAndAnUnreadableTermKeepsItClosed) {
        Load(Format(kDialog, "probe.dialog and probe.level == 3"));
        probe.dialog = true;
        Settle();
        EXPECT_FALSE(AnyPopupOpen());

        probe.level = 3;
        Settle();
        EXPECT_TRUE(AnyPopupOpen());

        probe.dialog = false;
        Settle();
        EXPECT_FALSE(AnyPopupOpen());

        Load(Format(kDialog, "probe.dialog or probe.nothing == 3"));
        probe.dialog = true;
        Settle();
        EXPECT_TRUE(AnyPopupOpen()) << "an alternative that holds opens it whatever the others read";
        probe.dialog = false;
        Settle();
        EXPECT_FALSE(AnyPopupOpen());

        LoadWithIssues(Format(kDialog, "probe.dialog and and"));
        probe.dialog = true;
        Settle();
        EXPECT_FALSE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAnExpressionWithACompareAllocatesNothing) {
        probe.level = 2;
        probe.flag = true;
        Load(Format(kConditions, "probe.mode != Error and probe.level < 5"));
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(PanelInterpreterTest, ACheckboxFlipsTheBoundBoolAndAToggleKeepsItsIdWhileItsLabelTextChanges) {
        Load(kControls);
        const ImVec2 flag = Locate(Chain(HostId(), {"chk", "Flag###chk"}));
        ClickAt(flag);
        EXPECT_TRUE(probe.flag);
        ClickAt(flag);
        EXPECT_FALSE(probe.flag);
        EXPECT_EQ(clicks, 0) << "a control writes its field and runs no command";

        const ImGuiID toggle = Chain(HostId(), {"tog", "###tog"});
        probe.title = "Hidden";
        Settle();
        ClickAt(Locate(toggle));
        EXPECT_TRUE(probe.hidden);

        probe.title = "Hidden by the user";
        Settle();
        ClickAt(Locate(toggle));
        EXPECT_FALSE(probe.hidden);
    }

    TEST_F(PanelInterpreterTest, AToggleIsFilledWhileOn) {
        probe.title = "Hidden";
        Load(kControls);
        const ImU32 fill = ImGui::GetColorU32(GetImGuiPalette().accentMuted);
        const auto filled = [&] {
            size_t count = 0;
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                count += vertex.col == fill ? 1 : 0;
            }
            return count;
        };
        const size_t before = filled();
        probe.hidden = true;
        Settle();
        EXPECT_GT(filled(), before);
    }

    TEST_F(PanelInterpreterTest, AComboOverAnEnumSetsTheValueByTheNameClicked) {
        Load(kControls);
        PickFromCombo(Chain(HostId(), {"mode", "##mode"}), static_cast<int>(PanelTextStyle::Warning), "Warning");
        EXPECT_EQ(probe.mode, PanelTextStyle::Warning);
    }

    TEST_F(PanelInterpreterTest, AComboOverAnIndexAndAListSetsTheIndexOfTheRowClicked) {
        Load(kControls);
        PickFromCombo(Chain(HostId(), {"pick", "##pick"}), 2, "gamma");
        EXPECT_EQ(probe.pick, 2);
    }

    TEST_F(PanelInterpreterTest, AComboOverATextAndAListOfRowsSetsTheTextOfTheRowClicked) {
        Load(kControls);
        PickFromCombo(Chain(HostId(), {"choice", "##choice"}), 1, "beta");
        EXPECT_EQ(probe.choice, "beta");
    }

    TEST_F(PanelInterpreterTest, AComboWhoseIndexIsOutOfRangeShowsAQuestionMarkInsteadOfARowAndStillSetsAnIndex) {
        probe.pick = 1;
        Load(kControls);
        const int row = HostVertices();
        probe.pick = 9;
        Settle();
        EXPECT_LT(HostVertices(), row);

        PickFromCombo(Chain(HostId(), {"pick", "##pick"}), 0, "alpha");
        EXPECT_EQ(probe.pick, 0);
    }

    TEST_F(PanelInterpreterTest, ANumberDragsWithinItsBoundsAndAnIntegerFieldStaysAnInteger) {
        Load(kControls);
        const ImVec2 level = Locate(Chain(HostId(), {"level", "##level"}));
        DragBy(level, 300.0f);
        EXPECT_EQ(probe.level, 5);
        DragBy(level, -300.0f);
        EXPECT_EQ(probe.level, 0);
        DragBy(level, 12.0f);
        EXPECT_GT(probe.level, 0);
        EXPECT_LE(probe.level, 5);
    }

    TEST_F(PanelInterpreterTest, ASliderOverAFloatStaysBetweenItsBounds) {
        Load(kControls);
        const ImVec2 gain = Locate(Chain(HostId(), {"gain", "##gain"}));
        DragBy(gain, 600.0f);
        EXPECT_FLOAT_EQ(probe.gain, 1.0f);
        DragBy(gain, -600.0f);
        EXPECT_FLOAT_EQ(probe.gain, 0.0f);
    }

    TEST_F(PanelInterpreterTest, ControlsOverAGoneSourceAreAQuestionMarkAndWriteNothing) {
        Load(R"(
<PanelAsset id="Gone" title="Gone">
  <elements>
    <element type="PanelCheckbox" id="chk" bind="nobody.flag" text="Flag"/>
    <element type="PanelCombo" id="mode" bind="nobody.mode"/>
    <element type="PanelNumber" id="level" bind="probe.nothing"/>
  </elements>
</PanelAsset>)");
        EXPECT_FALSE(TryLocate(Chain(HostId(), {"chk", "Flag###chk"})).has_value());
        EXPECT_FALSE(TryLocate(Chain(HostId(), {"level", "##level"})).has_value());
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfACheckboxAComboAndANumberAllocatesNothingClosedOrOpen) {
        Load(kControls);
        const auto steady = [&] {
            for (int i = 0; i < kWarmUpFrames; ++i) {
                Frame();
            }
            ExpectNoAllocations([&] {
                for (int i = 0; i < kSteadyFrames; ++i) {
                    Frame();
                }
            });
        };
        steady();

        ClickAt(Locate(Chain(HostId(), {"choice", "##choice"})));
        ASSERT_NE(ComboPopup(), nullptr);
        steady();
    }

    TEST_F(PanelInterpreterTest, ASelectableRowIsDrawnSelectedOnlyWhereItsFieldIsTrue) {
        Load(Format(kSelectedRows, "selected"));
        const size_t none = SelectedVertices();

        probe.rows[1].selected = true;
        Settle();
        const size_t one = SelectedVertices();
        EXPECT_GT(one, none);

        probe.rows[2].selected = true;
        Settle();
        EXPECT_EQ(SelectedVertices() - one, one - none);

        // A field that cannot be read selects nothing, unlike a condition
        Load(Format(kSelectedRows, "nothing"));
        EXPECT_EQ(SelectedVertices(), none);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameWithAChildAClosedPopupAndAnOpenPopupAllocatesNothing) {
        Load(Format(kDialog, "probe.dialog"));
        const auto steady = [&] {
            for (int i = 0; i < kWarmUpFrames; ++i) {
                Frame();
            }
            ExpectNoAllocations([&] {
                for (int i = 0; i < kSteadyFrames; ++i) {
                    Frame();
                }
            });
        };
        steady();

        probe.dialog = true;
        steady();
        EXPECT_TRUE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, ATabBarDrawsOnlyTheSelectedTabAndClickingAnotherTabSwitchesToIt) {
        Load(kTabs);
        const ImGuiTabBar* bar = ImGui::GetCurrentContext()->TabBars.GetByKey(Chain(HostId(), {"bar", "##bar"}));
        ASSERT_NE(bar, nullptr);
        ASSERT_EQ(bar->Tabs.Size, 2);
        const ImGuiID first = bar->Tabs[0].ID;
        const ImGuiID second = bar->Tabs[1].ID;

        EXPECT_TRUE(TryLocate(Chain(first, {"goA", "GoA"})).has_value());
        EXPECT_FALSE(TryLocate(Chain(second, {"goB", "GoB"})).has_value()) << "a tab that is not selected draws nothing";

        ClickAt(Locate(second));
        Settle();
        EXPECT_FALSE(TryLocate(Chain(first, {"goA", "GoA"})).has_value());
        ClickAt(Locate(Chain(second, {"goB", "GoB"})));
        EXPECT_EQ(lastCommand, "probe.b");
    }

    TEST_F(PanelInterpreterTest, ATabOutsideABarAndAStrayChildOfABarDrawNoImGuiTabAndCrashNothing) {
        LoadWithIssues(kTabOutsideABar);
        EXPECT_FALSE(TryLocate(Chain(HostId(), {"bar", "stray", "Stray"})).has_value());
        const ImGuiTabBar* bar = ImGui::GetCurrentContext()->TabBars.GetByKey(Chain(HostId(), {"bar", "##bar"}));
        ASSERT_NE(bar, nullptr);
        EXPECT_EQ(bar->Tabs.Size, 1);
    }

    TEST_F(PanelInterpreterTest, EscOnAPopupNotesItsCloseCommandOnceAndTheModelClearingTheBoolLetsItOpenAgain) {
        Load(kClosingDialog);
        probe.dialog = true;
        Settle();
        ASSERT_TRUE(AnyPopupOpen());

        PressEscape();
        EXPECT_FALSE(AnyPopupOpen());
        EXPECT_EQ(lastCommand, "probe.close");
        EXPECT_EQ(clicks, 1);

        // The model has not cleared the bool yet: the popup neither reopens nor repeats the command
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }
        EXPECT_FALSE(AnyPopupOpen());
        EXPECT_EQ(clicks, 1);

        probe.dialog = false;
        Settle();
        probe.dialog = true;
        Settle();
        EXPECT_TRUE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, APopupWithNoCloseCommandIgnoresEsc) {
        Load(Format(kDialog, "probe.dialog"));
        probe.dialog = true;
        Settle();
        ASSERT_TRUE(AnyPopupOpen());

        PressEscape();
        Settle();
        EXPECT_TRUE(AnyPopupOpen());
        EXPECT_FALSE(result.clicked);
    }

    TEST_F(PanelInterpreterTest, AnInputWithFocusInAPopupTakesTheKeyboardOnAppearingAndEnterNotesItsSubmitCommand) {
        Load(kClosingDialog);
        probe.dialog = true;
        Settle();
        ASSERT_TRUE(AnyPopupOpen());

        // No click on the field: the focus is the popup's doing
        Type("ab");
        EXPECT_EQ(probe.filter, "ab");

        ImGuiIO& io = ImGui::GetIO();
        clicks = 0;
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Frame();
        EXPECT_EQ(lastCommand, "probe.submit") << "the commit of the same frame must not hide the submit";
        EXPECT_EQ(probe.filter, "ab");
    }

    TEST_F(PanelInterpreterTest, ASliderNotesItsCommitCommandOnceOnRelease) {
        Load(kCommits);
        const ImVec2 gain = Locate(Chain(HostId(), {"gain", "##gain"}));
        clicks = 0;
        DragBy(gain, 100.0f);
        EXPECT_NE(probe.gain, 0.5f);
        EXPECT_EQ(lastCommand, "probe.commitGain");
        EXPECT_EQ(clicks, 1);
    }

    TEST_F(PanelInterpreterTest, AnInputNotesItsCommitCommandWhenLeftAfterAnEditAndNotWhenLeftUntouched) {
        Load(kCommits);
        const ImVec2 field = Locate(Chain(HostId(), {"text", "##text"}));
        const ImVec2 elsewhere(kHostSize.x - 2.0f, kHostSize.y - 2.0f);
        ClickAt(field);
        ClickAt(elsewhere);
        EXPECT_TRUE(lastCommand.empty());

        ClickAt(field);
        Type("x");
        EXPECT_TRUE(lastCommand.empty()) << "typing alone saves nothing";
        ClickAt(elsewhere);
        Settle();
        EXPECT_EQ(lastCommand, "probe.commitText");
        EXPECT_EQ(probe.filter, "x");
    }

    TEST_F(PanelInterpreterTest, AnInputHoldsATextOfFiveHundredBytesWithoutCuttingIt) {
        probe.filter.assign(500, 'a');
        Load(kInput);
        const String before = probe.filter;
        Settle();
        EXPECT_EQ(probe.filter, before);

        ClickAt(Locate(Chain(HostId(), {"filter", "##filter"})));
        Type("b");
        ASSERT_EQ(probe.filter.size(), before.size() + 1);
        EXPECT_NE(probe.filter.find('b'), String::npos);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfATabBarAllocatesNothing) {
        Load(kTabs);
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAnOpenPopupWithAFocusedInputAllocatesNothing) {
        Load(kClosingDialog);
        probe.dialog = true;
        for (int i = 0; i < kInputWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
        EXPECT_TRUE(AnyPopupOpen());
    }

}  // namespace BECore::Tests
