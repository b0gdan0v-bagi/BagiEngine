#include <BECore/Input/ImGuiItemIndex.h>
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

        constexpr const char* kCentredRow = R"(
<PanelAsset id="Centred" title="Centred">
  <elements>
    <element type="PanelRow" id="mid" align="Center">
      <elements>
        <element type="PanelButton" id="first" text="Aaaa" command="probe.first"/>
        <element type="PanelButton" id="second" text="Bbbbbbbb" command="probe.second" visible="!probe.hidden"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kFilledButton = R"(
<PanelAsset id="Filled" title="Filled">
  <elements>
    <element type="PanelButton" id="fill" text="Fill" fill="{}" fillWhen="probe.flag" command="probe.fill"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kRepeatButton = R"(
<PanelAsset id="Repeat" title="Repeat">
  <elements>
    <element type="PanelButton" id="step" text="Step" repeat="{}" command="probe.step"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kIconOnlyButton = R"(
<PanelAsset id="IconOnly" title="IconOnly">
  <elements>
    <element type="PanelButton" id="tool" icon="check" command="probe.tool" tooltip="Tool"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kToolbar = R"(
<PanelAsset id="Toolbar" title="Toolbar">
  <elements>
    <element type="PanelRow" id="mid" align="Center">
      <elements>
        <element type="PanelButton" id="play" icon="check" fill="Accent" command="probe.play"/>
        <element type="PanelButton" id="pause" icon="check" fill="Muted" fillWhen="probe.flag" command="probe.pause"/>
        <element type="PanelButton" id="step" icon="check" repeat="true" command="probe.step"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kMenus = R"(
<PanelAsset id="Menus" title="Menus" window="Bar" open="true">
  <elements>
    <element type="PanelMenu" id="file" title="File" openCommand="probe.opened">
      <elements>
        <element type="PanelMenuItem" id="open" text="Open" command="probe.open" argument="a.xml"/>
        <element type="PanelMenuItem" id="save" text="Save" icon="check" command="probe.save" enableWhen="probe.flag"/>
        <element type="PanelSeparator" id="line"/>
        <element type="PanelMenu" id="recent" title="Recent" openCommand="probe.recentOpened">
          <elements><element type="PanelMenuItem" id="last" text="Last" command="probe.last"/></elements>
        </element>
      </elements>
    </element>
    <element type="PanelMenu" id="edit" title="Edit" enableWhen="probe.flag">
      <elements>
        <element type="PanelMenuItems" id="names" source="probe.options" command="probe.pickName"/>
        <element type="PanelMenuItems" id="rowNames" source="probe.rows" itemField="name" command="probe.pickRow"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kMenuOutsideABar = R"(
<PanelAsset id="Loose" title="Loose">
  <elements>
    <element type="PanelMenu" id="file" title="File">
      <elements><element type="PanelMenuItem" id="open" text="Open" command="probe.open"/></elements>
    </element>
    <element type="PanelMenuItem" id="stray" text="Stray" command="probe.stray"/>
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

        /// The controls of a toolbar: switched off by `probe.flag`, a combo whose `selected` rows are separators, and typed numbers
        constexpr const char* kToolbarControls = R"(
<PanelAsset id="ToolbarControls" title="ToolbarControls">
  <elements>
    <element type="PanelCheckbox" id="rotate" bind="probe.hidden" text="Rotate" icon="check" enableWhen="probe.flag"/>
    <element type="PanelCombo" id="size" bind="probe.pick" items="probe.rows" itemField="name" separatorField="selected" width="10" enableWhen="probe.flag"/>
    <element type="PanelNumber" id="level" bind="probe.level" min="0" max="5" step="1" width="10" enableWhen="probe.flag"/>
    <element type="PanelNumber" id="side" bind="probe.level" input="true" min="0" max="50" width="10" commitCommand="probe.commitSide"/>
    <element type="PanelNumber" id="gain" bind="probe.gain" input="true" min="0" max="1" width="10"/>
  </elements>
</PanelAsset>)";

        /// A dropdown holding a checkbox, a button and a table of `Checkbox` cells, and an icon-only one turned off by `probe.hidden`
        constexpr const char* kDropdowns = R"(
<PanelAsset id="Dropdowns" title="Dropdowns">
  <elements>
    <element type="PanelRow" id="bar">
      <elements>
        <element type="PanelDropdown" id="overlays" text="Overlays" icon="check" tooltip="Overlays">
          <elements>
            <element type="PanelCheckbox" id="grid" bind="probe.flag" text="Grid"/>
            <element type="PanelButton" id="reset" text="Reset" command="probe.reset"/>
            <element type="PanelTable" id="canvases" source="probe.rows" header="false">
              <columns><column type="Checkbox" id="on" field="selected" labelField="name" enableField="enabled"/></columns>
            </element>
          </elements>
        </element>
        <element type="PanelDropdown" id="snap" icon="check" enableWhen="!probe.hidden">
          <elements><element type="PanelButton" id="snapAll" text="All" command="probe.snap"/></elements>
        </element>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kCheckboxColumn = R"(
<PanelAsset id="Switches" title="Switches">
  <elements>
    <element type="PanelTable" id="rows" source="probe.rows" header="false">
      <columns><column type="Checkbox" id="on" field="selected" labelField="name" enableField="enabled"/></columns>
    </element>
  </elements>
</PanelAsset>)";

        /// The form controls of the Sprite Library: radios over an enum and over an integer, a multiline input, a stepper, a section that reports its open state
        /// to `probe.flag` and a button that fills its line
        constexpr int kNamesLines = 3;
        constexpr const char* kSpriteForm = R"(
<PanelAsset id="SpriteForm" title="SpriteForm">
  <elements>
    <element type="PanelRow" id="modes">
      <elements>
        <element type="PanelRadio" id="normal" bind="probe.mode" value="Normal" text="Normal"/>
        <element type="PanelRadio" id="error" bind="probe.mode" value="Error" text="Error" icon="check" enableWhen="!probe.hidden"/>
        <element type="PanelRadio" id="unknown" bind="probe.mode" value="Nothing" text="Nothing"/>
      </elements>
    </element>
    <element type="PanelRow" id="levels">
      <elements>
        <element type="PanelRadio" id="one" bind="probe.level" value="1" text="One"/>
        <element type="PanelRadio" id="three" bind="probe.level" value="3" text="Three"/>
      </elements>
    </element>
    <element type="PanelInput" id="names" bind="probe.filter" lines="3"/>
    <element type="PanelNumber" id="count" bind="probe.level" stepper="true" step="2" min="0" max="5" width="10"/>
    <element type="PanelSection" id="slice" title="Slice sheet" icon="check" openBind="probe.flag" defaultOpen="false">
      <elements><element type="PanelText" id="inside" bind="probe.title"/></elements>
    </element>
    <element type="PanelButton" id="add" text="Add" icon="check" width="-1" command="probe.add"/>
  </elements>
</PanelAsset>)";

        /// A provider with one chord, as the editor's hotkey map would be
        class FakeChords : public PanelChordText {
        public:
            StringView ChordOf(StringView command) override {
                return command == "probe.open" ? StringView(kChord) : StringView{};
            }

            static constexpr const char* kChord = "Ctrl+Shift+O";
        };

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
            ImGui::Begin(kHostName, nullptr, menuBar ? (hostFlags | ImGuiWindowFlags_MenuBar) : hostFlags);
            // The caller of a `Bar` asset provides the menu bar, as the editor's host does
            const bool inMenuBar = menuBar && ImGui::BeginMenuBar();
            interpreter.Draw(sources, result, &slots);
            if (inMenuBar) {
                ImGui::EndMenuBar();
            }
            ImGui::End();
            ImGui::Render();
            if (result.clicked) {
                lastCommand = result.command;
                lastArgument = result.argument;
                noted.push_back(result.command);
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

        /// The right edge of what the host has laid out, in the screen's coordinates
        static float ContentRight() {
            return ImGui::FindWindowByName(kHostName)->DC.CursorMaxPos.x;
        }

        static float ButtonWidth(const char* text) {
            return ImGui::CalcTextSize(text).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        /// How many vertices the host has drawn in @p color
        static size_t VerticesOf(ImU32 color) {
            size_t count = 0;
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                count += vertex.col == color ? 1 : 0;
            }
            return count;
        }

        /// How many frames of the run noted @p command
        [[nodiscard]] int TimesNoted(StringView command) const {
            int count = 0;
            for (const String& name : noted) {
                count += name == command ? 1 : 0;
            }
            return count;
        }

        /// The id of the menu @p elementId of the host's menu bar, which ImGui's `BeginMenuBar` puts under its own id
        static ImGuiID MenuId(const char* elementId, const char* title) {
            const String label = Format("{}###{}", title, elementId);
            return Chain(HostId(), {"##MenuBar", elementId, label.c_str()});
        }

        /// The popup of the menu that is open at @p depth (0: one of the bar); ImGui names it `<title>###Menu_<depth>`, and the window is found by the part after the marks
        static const ImGuiWindow* MenuPopup(int depth = 0) {
            const String name = Format("###Menu_{:02}", depth);
            const ImGuiWindow* popup = ImGui::FindWindowByName(name.c_str());
            EXPECT_NE(popup, nullptr) << "no menu is open at depth " << depth;
            return popup;
        }

        /// The id of an item of a menu's popup: the popup starts the id chain, then the element id and the label
        static ImGuiID ItemId(const ImGuiWindow& popup, const char* elementId, const char* label) {
            return Chain(popup.ID, {elementId, label});
        }

        /// Clicks the title of the menu @p elementId and lets the popup settle
        void OpenMenu(const char* elementId, const char* title) {
            ClickAt(Locate(MenuId(elementId, title)));
            Settle();
        }

        /// The id of the dropdown @p elementId of the row @p rowId: its button's label is the last link
        static ImGuiID DropdownButtonId(const char* rowId, const char* elementId, const char* label) {
            return Chain(HostId(), {rowId, elementId, label});
        }

        /// The popup of the dropdown @p elementId of the row @p rowId, null when it is closed: ImGui names a popup `##Popup_<id>`, the id being `##dropdown` under the element's id
        static const ImGuiWindow* DropdownPopup(const char* rowId, const char* elementId) {
            const ImGuiID id = Chain(HostId(), {rowId, elementId, "##dropdown"});
            const ImGuiWindow* popup = ImGui::FindWindowByName(Format("##Popup_{:08x}", id).c_str());
            return popup != nullptr && popup->Active ? popup : nullptr;
        }

        /// The id of the `Checkbox` cell of @p row in the table @p tableElement, which starts its chain at @p seed (the window it is in), labelled @p label
        static ImGuiID CheckboxCellId(ImGuiID seed, const char* tableElement, int row, const char* label) {
            const ImGuiID table = Chain(seed, {tableElement, "table"});
            return Chain(ImHashData(&row, sizeof(row), table), {"on", label});
        }

        /// What the item index recorded of the control @p id in the next frame: its label and rect; none when it was not drawn
        Optional<ImGuiItemRecord> RecordOf(ImGuiID id) {
            ImGuiItemIndex index;
            index.Arm(*ImGui::GetCurrentContext());
            Frame();
            const ImGuiItemRecord* found = index.FindById(id);
            Optional<ImGuiItemRecord> record = found != nullptr ? Optional<ImGuiItemRecord>(*found) : std::nullopt;
            index.Disarm();
            return record;
        }

        /// Whether the radio @p id is drawn selected: the dot of a selected radio is drawn in the check mark colour inside its rect
        bool RadioSelected(ImGuiID id) {
            const Optional<ImGuiItemRecord> record = RecordOf(id);
            EXPECT_TRUE(record.has_value()) << "radio not drawn";
            if (!record) {
                return false;
            }
            const ImU32 dot = ImGui::GetColorU32(ImGuiCol_CheckMark);
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                if (vertex.col == dot && vertex.pos.x >= record->minX && vertex.pos.x <= record->maxX && vertex.pos.y >= record->minY && vertex.pos.y <= record->maxY) {
                    return true;
                }
            }
            return false;
        }

        static ImGuiID RadioId(const char* rowId, const char* elementId, const char* label) {
            return Chain(HostId(), {rowId, elementId, label});
        }

        /// Frames the mouse rests on an item before ImGui shows its tooltip (the stationary and the short hover delay), with room to spare
        static constexpr int kTooltipFrames = 30;

        /// The width of the text of the tooltip that shows after the mouse rests at @p point; none when no tooltip shows
        Optional<float> TooltipWidthAt(ImVec2 point) {
            ImGui::GetIO().AddMousePosEvent(point.x, point.y);
            for (int i = 0; i < kTooltipFrames; ++i) {
                Frame();
            }
            const ImGuiWindow* tooltip = ImGui::FindWindowByName("##Tooltip_00");
            if (tooltip == nullptr || !tooltip->WasActive) {
                return std::nullopt;
            }
            return tooltip->ContentSize.x;
        }

        /// How many vertices of the host drawn in the text colour lie inside @p record: four per glyph that is drawn (a space draws none)
        static size_t TextVerticesIn(const ImGuiItemRecord& record) {
            const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
            size_t count = 0;
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                const bool inside = vertex.pos.x >= record.minX && vertex.pos.x <= record.maxX && vertex.pos.y >= record.minY && vertex.pos.y <= record.maxY;
                count += vertex.col == text && inside ? 1 : 0;
            }
            return count;
        }

        bool menuBar = false;
        ImGuiFontBuffers buffers;
        ImGuiFonts fonts;
        InterpreterProbe probe;
        PanelDataSources sources;
        PanelSourceHandle handle;
        PanelSlots slots;
        PanelInterpreter interpreter;
        PanelDrawResult result;
        Vector<String> noted;
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

    TEST_F(PanelInterpreterTest, AClickOnAMenuItemNotesItsCommandAndArgument) {
        menuBar = true;
        probe.flag = true;
        Load(kMenus);
        OpenMenu("file", "File");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);

        ClickAt(LocateInWindow(ItemId(*popup, "open", "Open###open"), *popup));
        EXPECT_EQ(lastCommand, "probe.open");
        EXPECT_EQ(lastArgument, "a.xml");
        EXPECT_EQ(TimesNoted("probe.open"), 1);
    }

    TEST_F(PanelInterpreterTest, AMenuNotesItsOpenCommandOnTheFirstOpenFrameOnlyAndAgainAfterAClose) {
        menuBar = true;
        Load(kMenus);
        EXPECT_EQ(TimesNoted("probe.opened"), 0);

        OpenMenu("file", "File");
        ASSERT_TRUE(AnyPopupOpen());
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        EXPECT_EQ(TimesNoted("probe.opened"), 1) << "the command is the opening, not every open frame";

        // A click on the title of an open menu closes it
        ClickAt(Locate(MenuId("file", "File")));
        Settle();
        ASSERT_FALSE(AnyPopupOpen());
        EXPECT_EQ(TimesNoted("probe.opened"), 1);

        OpenMenu("file", "File");
        EXPECT_EQ(TimesNoted("probe.opened"), 2);
    }

    TEST_F(PanelInterpreterTest, ASubmenuNotesItsOwnOpenCommandWhenItOpens) {
        menuBar = true;
        Load(kMenus);
        OpenMenu("file", "File");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);
        EXPECT_EQ(TimesNoted("probe.recentOpened"), 0) << "a closed submenu is not drawn";

        ClickAt(LocateInWindow(ItemId(*popup, "recent", "Recent###recent"), *popup));
        Settle();
        EXPECT_EQ(TimesNoted("probe.recentOpened"), 1);
        EXPECT_NE(MenuPopup(1), nullptr);
    }

    TEST_F(PanelInterpreterTest, ADisabledItemIsNotClicked) {
        menuBar = true;
        probe.flag = true;
        Load(kMenus);
        OpenMenu("file", "File");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);
        const ImVec2 save = LocateInWindow(ItemId(*popup, "save", ICON_LC_CHECK " Save###save"), *popup);

        probe.flag = false;
        Settle();
        ClickAt(save);
        EXPECT_EQ(TimesNoted("probe.save"), 0);
    }

    TEST_F(PanelInterpreterTest, ADisabledMenuDoesNotOpen) {
        menuBar = true;
        probe.flag = true;
        Load(kMenus);
        const ImVec2 title = Locate(MenuId("edit", "Edit"));

        probe.flag = false;
        Settle();
        ClickAt(title);
        Settle();
        EXPECT_FALSE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, AMenuItemsElementDrawsOneItemPerRowWithTheRowAsLabelAndArgument) {
        menuBar = true;
        probe.flag = true;
        Load(kMenus);
        OpenMenu("edit", "Edit");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);

        const ImGuiID names = Chain(popup->ID, {"names"});
        const auto rowId = [&](int row, const char* label) { return Chain(ImHashData(&row, sizeof(row), names), {label}); };
        ClickAt(LocateInWindow(rowId(2, "gamma###names"), *popup));
        EXPECT_EQ(lastCommand, "probe.pickName");
        EXPECT_EQ(lastArgument, "gamma");

        // The list changes under the open menu: the items follow it
        OpenMenu("edit", "Edit");
        probe.options = {"one"};
        Settle();
        EXPECT_TRUE(TryLocateIn(rowId(0, "one###names"), popup->Pos, ImVec2(popup->Pos.x + popup->Size.x, popup->Pos.y + popup->Size.y)).has_value());
    }

    TEST_F(PanelInterpreterTest, AMenuItemsElementOverRowsReadsTheItemFieldOfEachRow) {
        menuBar = true;
        probe.flag = true;
        Load(kMenus);
        OpenMenu("edit", "Edit");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);

        const ImGuiID rows = Chain(popup->ID, {"rowNames"});
        const int row = 1;
        ClickAt(LocateInWindow(Chain(ImHashData(&row, sizeof(row), rows), {"beta###rowNames"}), *popup));
        EXPECT_EQ(lastCommand, "probe.pickRow");
        EXPECT_EQ(lastArgument, "beta");
    }

    TEST_F(PanelInterpreterTest, TheChordOfAnItemCommandWidensItsMenuWhereAProviderIsSet) {
        menuBar = true;
        Load(kMenus);
        OpenMenu("file", "File");
        const ImGuiWindow* popup = MenuPopup();
        ASSERT_NE(popup, nullptr);
        const float plain = popup->Size.x;

        FakeChords chords;
        interpreter.SetChordText(&chords);
        Settle();
        // ImGui keeps the widest column a menu has had, so the order is plain first
        EXPECT_GT(popup->Size.x, plain + ImGui::CalcTextSize(FakeChords::kChord).x * 0.9f);
    }

    TEST_F(PanelInterpreterTest, AMenuOrItemOutsideAMenuBarOrPopupIsAQuestionMarkAndOpensNothing) {
        LoadWithIssues(kMenuOutsideABar);
        EXPECT_GT(ContentHeight(), 0.0f);
        EXPECT_FALSE(TryLocate(Chain(HostId(), {"file", "File###file"})).has_value());
        EXPECT_FALSE(TryLocate(Chain(HostId(), {"stray", "Stray###stray"})).has_value());
        EXPECT_EQ(clicks, 0);
        EXPECT_FALSE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAMenuBarAllocatesNothingClosedOrOpenWithAChordProvider) {
        menuBar = true;
        probe.flag = true;
        FakeChords chords;
        interpreter.SetChordText(&chords);
        Load(kMenus);
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

        OpenMenu("file", "File");
        ASSERT_TRUE(AnyPopupOpen());
        steady();
        EXPECT_TRUE(AnyPopupOpen());
    }

    TEST_F(PanelInterpreterTest, ACentredRowSitsInTheMiddleFromTheSecondFrameAndStaysThereAndFollowsAChangedWidth) {
        // The first frame has no width to centre by and draws at the start
        const PanelLoadResult loaded = LoadPanelAsset(kCentredRow);
        ASSERT_TRUE(loaded.asset);
        interpreter.Prepare(loaded.asset);
        Frame();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float group = ButtonWidth("Aaaa") + spacing + ButtonWidth("Bbbbbbbb");
        const float left = ImGui::FindWindowByName(kHostName)->Pos.x + ImGui::GetStyle().WindowPadding.x;
        EXPECT_NEAR(ContentRight(), left + group, 1.0f);

        const float middle = kHostSize.x * 0.5f;
        Frame();
        EXPECT_NEAR(ContentRight() - group * 0.5f, middle, 1.0f);

        const float settled = ContentRight();
        Frame();
        EXPECT_FLOAT_EQ(ContentRight(), settled);

        // One child fewer: the group is narrower and is centred again one frame later
        probe.hidden = true;
        Settle();
        EXPECT_NEAR(ContentRight() - ButtonWidth("Aaaa") * 0.5f, middle, 1.0f);
    }

    TEST_F(PanelInterpreterTest, AFillPaintsTheButtonWithThePalettesColourOnlyWhileFillWhenReadsTrue) {
        const ImGuiPalette& palette = GetImGuiPalette();
        struct FillCase {
            const char* fill;
            ImVec4 color;
        };
        for (const auto& [fill, color] : {FillCase{"Muted", palette.accentMuted}, FillCase{"Accent", palette.accent}}) {
            Load(Format(kFilledButton, fill));
            // The previous round's click left the mouse on the button, and a hovered button has its own colour
            ImGui::GetIO().AddMousePosEvent(kHostSize.x - 1.0f, kHostSize.y - 1.0f);
            Settle();
            const ImU32 expected = ImGui::GetColorU32(color);
            probe.flag = false;
            Settle();
            const size_t without = VerticesOf(expected);

            probe.flag = true;
            Settle();
            EXPECT_GT(VerticesOf(expected), without) << fill;

            // A click still works on a filled button
            ClickAt(Locate(Chain(HostId(), {"fill", "Fill"})));
            EXPECT_EQ(lastCommand, "probe.fill");
        }
    }

    TEST_F(PanelInterpreterTest, AHeldRepeatButtonNotesAClickOnEveryRepeatFrameAndAPlainOneDoesNot) {
        ImGuiIO& io = ImGui::GetIO();
        io.KeyRepeatDelay = kFrameSeconds;
        io.KeyRepeatRate = kFrameSeconds;
        constexpr int kHeldFrames = 6;

        const auto hold = [&](const char* repeat) {
            Load(Format(kRepeatButton, repeat));
            const ImVec2 point = Locate(Chain(HostId(), {"step", "Step"}));
            noted.clear();
            io.AddMousePosEvent(point.x, point.y);
            Frame();
            io.AddMouseButtonEvent(0, true);
            for (int i = 0; i < kHeldFrames; ++i) {
                Frame();
            }
            const int whileHeld = TimesNoted("probe.step");
            io.AddMouseButtonEvent(0, false);
            Frame();
            return whileHeld;
        };
        EXPECT_GE(hold("true"), kHeldFrames - 1);
        EXPECT_EQ(hold("false"), 0) << "a plain button clicks when it is let go";
    }

    TEST_F(PanelInterpreterTest, AButtonWithAnIconAndNoTextIsASquareOfTheFrameHeightWithTheElementIdForItsId) {
        Load(kIconOnlyButton);
        const float left = ImGui::FindWindowByName(kHostName)->Pos.x + ImGui::GetStyle().WindowPadding.x;
        EXPECT_NEAR(ContentRight() - left, ImGui::GetFrameHeight(), 0.5f);

        ClickAt(Locate(Chain(HostId(), {"tool", ICON_LC_CHECK "##tool"})));
        EXPECT_EQ(lastCommand, "probe.tool");
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfACentredRowOfFilledRepeatingIconButtonsAllocatesNothing) {
        Load(kToolbar);
        probe.flag = true;
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    TEST_F(PanelInterpreterTest, ACheckboxAComboAndANumberUnderAFalseEnableWhenWriteNothingAndWriteAgainWhenItTurnsTrue) {
        probe.flag = true;
        Load(kToolbarControls);
        const ImVec2 rotate = Locate(Chain(HostId(), {"rotate", "###rotate"}));
        const ImGuiID size = Chain(HostId(), {"size", "##size"});
        const ImVec2 sizePoint = Locate(size);
        const ImVec2 level = Locate(Chain(HostId(), {"level", "##level"}));

        probe.flag = false;
        Settle();
        ClickAt(rotate);
        EXPECT_FALSE(probe.hidden);
        ClickAt(sizePoint);
        Settle();
        EXPECT_FALSE(AnyPopupOpen());
        DragBy(level, 300.0f);
        EXPECT_EQ(probe.level, 0);

        probe.flag = true;
        Settle();
        ClickAt(rotate);
        EXPECT_TRUE(probe.hidden);
        PickFromCombo(size, 2, "gamma");
        EXPECT_EQ(probe.pick, 2);
        DragBy(level, 300.0f);
        EXPECT_EQ(probe.level, 5);
    }

    TEST_F(PanelInterpreterTest, ACheckboxShowsTheGlyphOfItsIconBeforeItsText) {
        Load(kToolbarControls);
        ImGuiItemIndex index;
        index.Arm(*ImGui::GetCurrentContext());
        Frame();
        const ImGuiItemRecord* rotate = index.FindById(Chain(HostId(), {"rotate", "###rotate"}));
        ASSERT_NE(rotate, nullptr);
        EXPECT_EQ(rotate->label, ICON_LC_CHECK " Rotate###rotate");
        index.Disarm();
    }

    TEST_F(PanelInterpreterTest, AComboRowMarkedAsASeparatorIsNoChoiceAndTheRowsAroundItAre) {
        probe.flag = true;
        probe.rows[1].selected = true;
        Load(kToolbarControls);
        const ImGuiID size = Chain(HostId(), {"size", "##size"});
        ClickAt(Locate(size));
        Settle();
        const ImGuiWindow* popup = ComboPopup();
        ASSERT_NE(popup, nullptr);
        const ImVec2 popupEnd(popup->Pos.x + popup->Size.x, popup->Pos.y + popup->Size.y);
        EXPECT_FALSE(TryLocateIn(ComboRowId(*popup, 1, "beta"), popup->Pos, popupEnd).has_value());
        ClickAt(LocateInWindow(ComboRowId(*popup, 2, "gamma"), *popup));
        Settle();
        EXPECT_EQ(probe.pick, 2);
        PickFromCombo(size, 0, "alpha");
        EXPECT_EQ(probe.pick, 0);

        // A value that names the separator row shows `?`, which draws less than the row's text
        probe.pick = 1;
        Settle();
        const int onSeparator = HostVertices();
        probe.rows[1].selected = false;
        Settle();
        EXPECT_GT(HostVertices(), onSeparator);
    }

    TEST_F(PanelInterpreterTest, ATypedNumberIsWrittenClampedOnlyWhenLeftOrConfirmedAndNotesItsCommitOnceThen) {
        Load(kToolbarControls);
        const ImVec2 side = Locate(Chain(HostId(), {"side", "##side"}));
        const ImVec2 elsewhere(kHostSize.x - 2.0f, kHostSize.y - 2.0f);
        ClickAt(side);
        Type("99");
        EXPECT_EQ(probe.level, 0) << "typing alone writes nothing";
        EXPECT_EQ(TimesNoted("probe.commitSide"), 0);
        ClickAt(elsewhere);
        Settle();
        EXPECT_EQ(probe.level, 50);
        EXPECT_EQ(TimesNoted("probe.commitSide"), 1);

        ClickAt(side);
        Type("7");
        EXPECT_EQ(probe.level, 50);
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Settle();
        EXPECT_EQ(probe.level, 7);
        EXPECT_EQ(TimesNoted("probe.commitSide"), 2);

        // A float field parses a float and is clamped the same way
        ClickAt(Locate(Chain(HostId(), {"gain", "##gain"})));
        Type("2.5");
        ClickAt(elsewhere);
        Settle();
        EXPECT_FLOAT_EQ(probe.gain, 1.0f);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAnOpenSeparatedComboAndOfAnActiveTypedNumberAllocatesNothing) {
        probe.flag = true;
        probe.rows[1].selected = true;
        Load(kToolbarControls);
        const auto steady = [&](int warmUpFrames) {
            for (int i = 0; i < warmUpFrames; ++i) {
                Frame();
            }
            ExpectNoAllocations([&] {
                for (int i = 0; i < kSteadyFrames; ++i) {
                    Frame();
                }
            });
        };
        ClickAt(Locate(Chain(HostId(), {"size", "##size"})));
        ASSERT_NE(ComboPopup(), nullptr);
        steady(kWarmUpFrames);

        ClickAt(ImVec2(kHostSize.x - 2.0f, kHostSize.y - 2.0f));
        ClickAt(Locate(Chain(HostId(), {"side", "##side"})));
        Type("3");
        steady(kInputWarmUpFrames);
        EXPECT_EQ(probe.level, 0);
    }

    TEST_F(PanelInterpreterTest, ADropdownOpensItsPopupOnlyWhenItsButtonIsClickedAndItsChildrenWorkInIt) {
        const PanelLoadResult loaded = LoadPanelAsset(kDropdowns);
        const PanelElement& row = Require(At(Require(loaded.asset).elements, 0));
        EXPECT_TRUE(PanelInterpreter::CanDraw(Require(At(row.elements, 0))));
        Load(kDropdowns);
        EXPECT_EQ(DropdownPopup("bar", "overlays"), nullptr);

        ClickAt(Locate(DropdownButtonId("bar", "overlays", ICON_LC_CHECK " Overlays##overlays")));
        Settle();
        const ImGuiWindow* popup = DropdownPopup("bar", "overlays");
        ASSERT_NE(popup, nullptr);
        const ImGuiWindow* host = ImGui::FindWindowByName(kHostName);
        ASSERT_NE(host, nullptr);
        EXPECT_GT(popup->Pos.y, host->Pos.y + host->WindowPadding.y) << "the popup opens under its button, not at its top";

        ClickAt(LocateInWindow(Chain(popup->ID, {"reset", "Reset"}), *popup));
        EXPECT_EQ(lastCommand, "probe.reset");
        ASSERT_EQ(DropdownPopup("bar", "overlays"), popup) << "a click inside keeps the popup open";
        ClickAt(LocateInWindow(Chain(popup->ID, {"grid", "Grid###grid"}), *popup));
        EXPECT_TRUE(probe.flag);
        ClickAt(LocateInWindow(CheckboxCellId(popup->ID, "canvases", 2, "gamma##on"), *popup));
        EXPECT_TRUE(probe.rows[2].selected);

        // ImGui closes it itself, on Esc with keyboard navigation (which the editor turns on) and on a click outside
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        PressEscape();
        EXPECT_EQ(DropdownPopup("bar", "overlays"), nullptr);
        ClickAt(Locate(DropdownButtonId("bar", "overlays", ICON_LC_CHECK " Overlays##overlays")));
        ASSERT_NE(DropdownPopup("bar", "overlays"), nullptr);
        ClickAt(ImVec2(kHostSize.x - 2.0f, kHostSize.y - 2.0f));
        Settle();
        EXPECT_EQ(DropdownPopup("bar", "overlays"), nullptr);
    }

    TEST_F(PanelInterpreterTest, ADropdownWhoseEnableWhenIsFalseDoesNotOpen) {
        Load(kDropdowns);
        const ImVec2 snap = Locate(DropdownButtonId("bar", "snap", ICON_LC_CHECK "##snap"));
        probe.hidden = true;
        Settle();
        ClickAt(snap);
        Settle();
        EXPECT_EQ(DropdownPopup("bar", "snap"), nullptr);

        probe.hidden = false;
        Settle();
        ClickAt(snap);
        Settle();
        EXPECT_NE(DropdownPopup("bar", "snap"), nullptr);
    }

    TEST_F(PanelInterpreterTest, ACheckboxColumnWritesTheBoolOfTheClickedRowLabelledByItsLabelFieldAndNotOfADisabledRow) {
        probe.rows[2].enabled = false;
        Load(kCheckboxColumn);
        ImGuiItemIndex index;
        index.Arm(*ImGui::GetCurrentContext());
        Frame();
        const ImGuiItemRecord* beta = index.FindById(CheckboxCellId(HostId(), "rows", 1, "beta##on"));
        ASSERT_NE(beta, nullptr);
        EXPECT_EQ(beta->label, "beta##on");
        index.Disarm();

        ClickAt(Locate(CheckboxCellId(HostId(), "rows", 1, "beta##on")));
        EXPECT_FALSE(probe.rows[0].selected);
        EXPECT_TRUE(probe.rows[1].selected);
        EXPECT_FALSE(probe.rows[2].selected);
        EXPECT_EQ(clicks, 0) << "a cell writes its row and runs no command";

        const ImGuiID gamma = CheckboxCellId(HostId(), "rows", 2, "gamma##on");
        probe.rows[2].enabled = true;
        Settle();
        const ImVec2 gammaPoint = Locate(gamma);
        probe.rows[2].enabled = false;
        Settle();
        ClickAt(gammaPoint);
        EXPECT_FALSE(probe.rows[2].selected);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameWithAnOpenDropdownAndItsCheckboxColumnAllocatesNothing) {
        Load(kDropdowns);
        ClickAt(Locate(DropdownButtonId("bar", "overlays", ICON_LC_CHECK " Overlays##overlays")));
        ASSERT_NE(DropdownPopup("bar", "overlays"), nullptr);
        for (int i = 0; i < kWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
        EXPECT_NE(DropdownPopup("bar", "overlays"), nullptr);
    }

    TEST_F(PanelInterpreterTest, TwoRadiosOverAnEnumAreSelectedByTheFieldAndAClickWritesTheOtherValue) {
        const PanelLoadResult loaded = LoadPanelAsset(kSpriteForm);
        const PanelElement& modes = Require(At(Require(loaded.asset).elements, 0));
        EXPECT_TRUE(PanelInterpreter::CanDraw(Require(At(modes.elements, 0))));
        Load(kSpriteForm);
        const ImGuiID normal = RadioId("modes", "normal", "Normal###normal");
        const ImGuiID error = RadioId("modes", "error", ICON_LC_CHECK " Error###error");
        EXPECT_TRUE(RadioSelected(normal));
        EXPECT_FALSE(RadioSelected(error));

        ClickAt(Locate(error));
        EXPECT_EQ(probe.mode, PanelTextStyle::Error);
        EXPECT_FALSE(RadioSelected(normal));
        EXPECT_TRUE(RadioSelected(error));
        EXPECT_EQ(clicks, 0) << "a radio writes its field and runs no command";

        ClickAt(Locate(normal));
        EXPECT_EQ(probe.mode, PanelTextStyle::Normal);
    }

    TEST_F(PanelInterpreterTest, ARadioOverAnIntegerIsSelectedByItsNumberAndAClickWritesIt) {
        Load(kSpriteForm);
        const ImGuiID three = RadioId("levels", "three", "Three###three");
        EXPECT_FALSE(RadioSelected(three));
        ClickAt(Locate(three));
        EXPECT_EQ(probe.level, 3);
        EXPECT_TRUE(RadioSelected(three));
        EXPECT_FALSE(RadioSelected(RadioId("levels", "one", "One###one")));
    }

    TEST_F(PanelInterpreterTest, ARadioWhoseValueTheEnumLacksIsNeverSelectedAndWritesNothing) {
        probe.mode = PanelTextStyle::Warning;
        Load(kSpriteForm);
        const ImGuiID unknown = RadioId("modes", "unknown", "Nothing###unknown");
        EXPECT_FALSE(RadioSelected(unknown));
        ClickAt(Locate(unknown));
        ClickAt(Locate(unknown));
        EXPECT_EQ(probe.mode, PanelTextStyle::Warning);
        EXPECT_FALSE(RadioSelected(unknown));
    }

    TEST_F(PanelInterpreterTest, ARadioUnderAFalseEnableWhenWritesNothing) {
        probe.hidden = true;
        Load(kSpriteForm);
        ClickAt(Locate(RadioId("modes", "error", ICON_LC_CHECK " Error###error")));
        EXPECT_EQ(probe.mode, PanelTextStyle::Normal);
    }

    TEST_F(PanelInterpreterTest, AMultilineInputIsItsLinesHighAndWritesATextWithANewLine) {
        Load(kSpriteForm);
        const ImGuiID names = Chain(HostId(), {"names", "##names"});
        const Optional<ImGuiItemRecord> record = RecordOf(names);
        ASSERT_TRUE(record.has_value());
        const float expected = static_cast<float>(kNamesLines) * ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
        EXPECT_NEAR(record->maxY - record->minY, expected, 1.0f);

        ClickAt(Locate(names));
        Type("a");
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        Frame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        Type("b");
        EXPECT_EQ(probe.filter, "a\nb");
        EXPECT_EQ(clicks, 0) << "Enter in a multiline field is a new line, never a command";
    }

    TEST_F(PanelInterpreterTest, AMultilineInputHoldsATextOfTwoThousandBytesWithoutCuttingIt) {
        constexpr size_t kLongText = 2000;
        probe.filter.assign(kLongText, 'x');
        Load(kSpriteForm);
        ClickAt(Locate(Chain(HostId(), {"names", "##names"})));
        Type("y");
        EXPECT_EQ(probe.filter.size(), kLongText + 1);
        EXPECT_EQ(eastl::count(probe.filter.begin(), probe.filter.end(), 'x'), static_cast<ptrdiff_t>(kLongText));
    }

    TEST_F(PanelInterpreterTest, AStepperButtonAddsTheStepAtOnceAndTheFieldStaysWithinItsBounds) {
        Load(kSpriteForm);
        const ImVec2 plus = Locate(Chain(HostId(), {"count", "##count", "+"}));
        ClickAt(plus);
        EXPECT_EQ(probe.level, 2);
        ClickAt(plus);
        ClickAt(plus);
        EXPECT_EQ(probe.level, 5);

        const ImVec2 minus = Locate(Chain(HostId(), {"count", "##count", "-"}));
        for (int i = 0; i < 3; ++i) {
            ClickAt(minus);
        }
        EXPECT_EQ(probe.level, 0);
    }

    TEST_F(PanelInterpreterTest, ASectionWritesItsOpenStateToItsOpenBindAndNeverFollowsTheField) {
        probe.flag = true;
        Load(kSpriteForm);
        EXPECT_FALSE(probe.flag) << "the closed header writes its state";

        const ImGuiID header = Chain(HostId(), {"slice", ICON_LC_CHECK " Slice sheet###slice"});
        const Optional<ImGuiItemRecord> record = RecordOf(header);
        ASSERT_TRUE(record.has_value());
        EXPECT_EQ(record->label, ICON_LC_CHECK " Slice sheet###slice");

        ClickAt(Locate(header));
        EXPECT_TRUE(probe.flag);
        ClickAt(Locate(header));
        EXPECT_FALSE(probe.flag);

        probe.flag = true;
        Settle();
        EXPECT_FALSE(probe.flag) << "the field does not open the header";
    }

    TEST_F(PanelInterpreterTest, AFillingButtonReachesTheRightEdgeOfItsLine) {
        Load(kSpriteForm);
        const Optional<ImGuiItemRecord> record = RecordOf(Chain(HostId(), {"add", ICON_LC_CHECK " Add"}));
        ASSERT_TRUE(record.has_value());
        EXPECT_NEAR(record->maxX, ImGui::FindWindowByName(kHostName)->WorkRect.Max.x, 1.0f);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfTheSpriteFormAllocatesNothingWithTheSectionOpenAndTheMultilineInputActive) {
        probe.title = "inside";
        Load(kSpriteForm);
        ClickAt(Locate(Chain(HostId(), {"slice", ICON_LC_CHECK " Slice sheet###slice"})));
        ASSERT_TRUE(probe.flag);
        ClickAt(Locate(Chain(HostId(), {"names", "##names"})));
        Type("a");
        for (int i = 0; i < kInputWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    namespace {

        constexpr const char* kBoundTooltips = R"(
<PanelAsset id="Tooltips" title="Tooltips">
  <elements>
    <element type="PanelRow" id="buttons">
      <elements>
        <element type="PanelButton" id="bound" text="Bound" command="probe.go" tooltip="Lit" tooltipBind="probe.title"/>
        <element type="PanelButton" id="broken" text="Broken" command="probe.go" tooltip="Lit" tooltipBind="probe.nothing"/>
      </elements>
    </element>
    <element type="PanelTable" id="plan" source="probe.rows">
      <columns>
        <column id="pick" type="Selectable" field="name" tooltipField="note"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kRowFieldCombo = R"(
<PanelAsset id="Combo" title="Combo">
  <elements>
    <element type="PanelCombo" id="preset" bind="probe.pick" items="probe.rows" itemField="name" enableField="enabled" tooltipField="note" previewBind="probe.choice" text="Preset"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kLabelledInput = R"(
<PanelAsset id="Input" title="Input">
  <elements>
    <element type="PanelInput" id="name" bind="probe.filter" text="Name"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kInputWidths = R"(
<PanelAsset id="Widths" title="Widths">
  <elements>
    <element type="PanelInput" id="narrow" bind="probe.filter" fill="false"/>
    <element type="PanelInput" id="wide" bind="probe.filter"/>
  </elements>
</PanelAsset>)";

        /// A text and a tab bar; the icon of each is spliced in by the test
        constexpr const char* kIconsTemplate = R"(
<PanelAsset id="Icons" title="Icons">
  <elements>
    <element type="PanelText" id="status" text="Ready" {}/>
    <element type="PanelTabs" id="tabs">
      <elements>
        <element type="PanelTab" id="run" title="{}" icon="check"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kUnitNumbers = R"(
<PanelAsset id="Units" title="Units">
  <elements>
    <element type="PanelNumber" id="padding" bind="probe.gain" min="0" max="100" slider="true" unit="%" decimals="0"/>
    <element type="PanelNumber" id="percent" bind="probe.level" min="0" max="100" slider="true" unit="%" decimals="2"/>
    <element type="PanelNumber" id="plain" bind="probe.gain" min="0" max="100" slider="true"/>
  </elements>
</PanelAsset>)";

        /// The glyphs drawn of `12 %` (a space draws none) and of ImGui's default float format, `12.000`
        constexpr size_t kPercentGlyphs = 3;
        constexpr size_t kDefaultFormatGlyphs = 6;
        constexpr size_t kVerticesPerGlyph = 4;

    }  // namespace

    TEST_F(PanelInterpreterTest, ABoundTooltipReplacesTheLiteralAndOneThatCannotBeReadLeavesTheLiteral) {
        probe.title = "A tooltip the model wrote";
        Load(kBoundTooltips);

        const Optional<float> bound = TooltipWidthAt(Locate(Chain(HostId(), {"buttons", "bound", "Bound"})));
        ASSERT_TRUE(bound.has_value());
        EXPECT_NEAR(*bound, ImGui::CalcTextSize(probe.title.c_str()).x, 1.0f);

        const Optional<float> broken = TooltipWidthAt(Locate(Chain(HostId(), {"buttons", "broken", "Broken"})));
        ASSERT_TRUE(broken.has_value());
        EXPECT_NEAR(*broken, ImGui::CalcTextSize("Lit").x, 1.0f);
    }

    TEST_F(PanelInterpreterTest, AColumnsTooltipFieldIsTheTextOfItsRow) {
        probe.rows[1].note = "The licence of the second row";
        Load(kBoundTooltips);
        const ImGuiID beta = Chain(TableCellChain("plan", 1, "pick"), {"beta##select"});
        const Optional<float> width = TooltipWidthAt(Locate(beta));
        ASSERT_TRUE(width.has_value());
        EXPECT_NEAR(*width, ImGui::CalcTextSize(probe.rows[1].note.c_str()).x, 1.0f);

        const ImGuiID alpha = Chain(TableCellChain("plan", 0, "pick"), {"alpha##select"});
        EXPECT_FALSE(TooltipWidthAt(Locate(alpha)).has_value()) << "an empty text shows no tooltip";
    }

    TEST_F(PanelInterpreterTest, AComboRowThatItsEnableFieldTurnsOffIsNeverPickedAndARowShowsItsTooltip) {
        probe.rows[1].enabled = false;
        probe.rows[2].note = "The third row";
        Load(kRowFieldCombo);
        ClickAt(Locate(Chain(HostId(), {"preset", "##preset"})));
        Settle();
        const ImGuiWindow* popup = ComboPopup();
        ASSERT_NE(popup, nullptr);

        ClickAt(LocateInWindow(ComboRowId(*popup, 1, "beta"), *popup));
        Settle();
        EXPECT_EQ(probe.pick, 0);
        EXPECT_TRUE(AnyPopupOpen()) << "a click on a disabled row keeps the combo open";

        const Optional<float> width = TooltipWidthAt(LocateInWindow(ComboRowId(*popup, 2, "gamma"), *popup));
        ASSERT_TRUE(width.has_value());
        EXPECT_NEAR(*width, ImGui::CalcTextSize(probe.rows[2].note.c_str()).x, 1.0f);

        ClickAt(LocateInWindow(ComboRowId(*popup, 2, "gamma"), *popup));
        Settle();
        EXPECT_EQ(probe.pick, 2);
    }

    TEST_F(PanelInterpreterTest, AComboShowsItsPreviewBindWhileClosedAndItsLabelAfterIt) {
        probe.choice.clear();
        Load(kRowFieldCombo);
        const int emptyPreview = HostVertices();
        probe.choice = "WWWW";
        Settle();
        EXPECT_EQ(HostVertices() - emptyPreview, static_cast<int>(probe.choice.size() * kVerticesPerGlyph)) << "the bound text is the preview, not the chosen row's";

        const Optional<ImGuiItemRecord> combo = RecordOf(Chain(HostId(), {"preset", "##preset"}));
        ASSERT_TRUE(combo.has_value());
        EXPECT_NEAR(ContentRight(), combo->maxX + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Preset").x, 1.0f);
    }

    TEST_F(PanelInterpreterTest, AnInputWithATextDrawsItAfterTheFieldAndOneThatDoesNotFillIsNarrower) {
        Load(kLabelledInput);
        const Optional<ImGuiItemRecord> labelled = RecordOf(Chain(HostId(), {"name", "##name"}));
        ASSERT_TRUE(labelled.has_value());
        EXPECT_NEAR(ContentRight(), labelled->maxX + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Name").x, 1.0f);

        Load(kInputWidths);
        const Optional<ImGuiItemRecord> narrow = RecordOf(Chain(HostId(), {"narrow", "##narrow"}));
        const Optional<ImGuiItemRecord> wide = RecordOf(Chain(HostId(), {"wide", "##wide"}));
        ASSERT_TRUE(narrow.has_value());
        ASSERT_TRUE(wide.has_value());
        EXPECT_NEAR(narrow->maxX - narrow->minX, labelled->maxX - labelled->minX, 1.0f) << "a field with a label and one that does not fill take the same default width";
        EXPECT_LT(narrow->maxX - narrow->minX, wide->maxX - wide->minX);
        EXPECT_NEAR(wide->maxX, ImGui::FindWindowByName(kHostName)->WorkRect.Max.x, 1.0f);
    }

    TEST_F(PanelInterpreterTest, ATextAndATabDrawTheGlyphOfTheirIconAndTheTabKeepsItsIdWhenItsTitleChanges) {
        Load(Format(kIconsTemplate, "", "Generate"));
        const int plain = HostVertices();
        Load(Format(kIconsTemplate, R"(icon="check")", "Generate"));
        EXPECT_EQ(HostVertices() - plain, static_cast<int>(kVerticesPerGlyph)) << "the glyph is drawn before the text";

        // The bar pushes its own id, the walk the tab's, and the label hashes from its `###` on
        const ImGuiID bar = Chain(HostId(), {"tabs", "##tabs"});
        const ImGuiID tabId = Chain(bar, {"run", "###run"});
        const Optional<ImGuiItemRecord> tab = RecordOf(tabId);
        ASSERT_TRUE(tab.has_value());
        EXPECT_NEAR(tab->maxX - tab->minX, ImGui::CalcTextSize(ICON_LC_CHECK " Generate").x + ImGui::GetStyle().FramePadding.x * 2.0f, 1.0f) << "the glyph and a space come before the title";

        Load(Format(kIconsTemplate, R"(icon="check")", "Images"));
        EXPECT_TRUE(RecordOf(tabId).has_value());
    }

    TEST_F(PanelInterpreterTest, ANumberWithAUnitShowsItAfterTheValueAndStillWritesTheNumber) {
        probe.gain = 12.0f;
        probe.level = 12;
        Load(kUnitNumbers);
        for (const char* id : {"padding", "percent"}) {
            const Optional<ImGuiItemRecord> record = RecordOf(Chain(HostId(), {id, Format("##{}", id).c_str()}));
            ASSERT_TRUE(record.has_value()) << id;
            EXPECT_EQ(TextVerticesIn(*record), kPercentGlyphs * kVerticesPerGlyph) << id << " shows `12 %`";
        }
        const Optional<ImGuiItemRecord> plain = RecordOf(Chain(HostId(), {"plain", "##plain"}));
        ASSERT_TRUE(plain.has_value());
        EXPECT_EQ(TextVerticesIn(*plain), kDefaultFormatGlyphs * kVerticesPerGlyph) << "no unit and no decimals keep ImGui's format";

        const Optional<ImGuiItemRecord> padding = RecordOf(Chain(HostId(), {"padding", "##padding"}));
        ASSERT_TRUE(padding.has_value());
        ClickAt(ImVec2(padding->minX + 2.0f, padding->CenterY()));
        Settle();
        EXPECT_LT(probe.gain, 12.0f) << "a click near the left end writes a smaller number";
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameWithABoundTooltipShownAndAnOpenComboOfRowFieldsAllocatesNothing) {
        probe.title = "A tooltip the model wrote";
        probe.rows[0].note = "First";
        probe.choice = "chosen";
        Load(kBoundTooltips);
        ASSERT_TRUE(TooltipWidthAt(Locate(Chain(HostId(), {"buttons", "bound", "Bound"}))).has_value());
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });

        Load(kRowFieldCombo);
        ClickAt(Locate(Chain(HostId(), {"preset", "##preset"})));
        Settle();
        const ImGuiWindow* popup = ComboPopup();
        ASSERT_NE(popup, nullptr);
        ASSERT_TRUE(TooltipWidthAt(LocateInWindow(ComboRowId(*popup, 0, "alpha"), *popup)).has_value());
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

    namespace {

        constexpr const char* kProgress = R"(
<PanelAsset id="Progress" title="Progress">
  <elements>
    <element type="PanelProgress" id="download" bind="probe.gain" textBind="probe.title" width="10"/>
  </elements>
</PanelAsset>)";

        constexpr const char* kTreeSection = R"(
<PanelAsset id="Tree" title="Tree">
  <elements>
    <element type="PanelSection" id="settings" title="Settings" look="Tree" defaultOpen="false" openBind="probe.flag">
      <elements>
        <element type="PanelButton" id="inside" text="Inside" command="probe.inside"/>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

        constexpr const char* kSaveAsDropdown = R"(
<PanelAsset id="SaveAs" title="Save as">
  <elements>
    <element type="PanelRow" id="bar">
      <elements>
        <element type="PanelDropdown" id="saveAs" text="Save as" openCommand="probe.opened">
          <elements>
            <element type="PanelInput" id="name" bind="probe.filter"/>
            <element type="PanelButton" id="save" text="Save" command="probe.save" closePopup="true"/>
          </elements>
        </element>
      </elements>
    </element>
  </elements>
</PanelAsset>)";

    }  // namespace

    TEST_F(PanelInterpreterTest, AProgressBarFillsItsFractionClampedAndShowsItsOverlayOrAQuestionMark) {
        EXPECT_TRUE(PanelInterpreter::CanDraw(*New<PanelProgress>()));
        const auto filledWidth = [] {
            const ImU32 bar = ImGui::GetColorU32(ImGuiCol_PlotHistogram);
            float left = FLT_MAX;
            float right = -FLT_MAX;
            for (const ImDrawVert& vertex : ImGui::FindWindowByName(kHostName)->DrawList->VtxBuffer) {
                if (vertex.col == bar) {
                    left = eastl::min(left, vertex.pos.x);
                    right = eastl::max(right, vertex.pos.x);
                }
            }
            return right > left ? right - left : 0.0f;
        };
        const auto textVertices = [] { return VerticesOf(ImGui::GetColorU32(ImGuiCol_Text)); };
        probe.gain = 0.25f;
        probe.title = "WWW";
        Load(kProgress);
        // The font size is known once a frame has run
        const float barWidth = 10.0f * ImGui::GetFontSize() - ImGui::GetStyle().FrameBorderSize * 2.0f;
        EXPECT_NEAR(filledWidth(), barWidth * 0.25f, 1.0f);
        EXPECT_EQ(textVertices(), probe.title.size() * kVerticesPerGlyph) << "the bound overlay";

        probe.gain = 2.0f;
        Settle();
        EXPECT_NEAR(filledWidth(), barWidth, 1.0f);

        const PanelLoadResult unreadable = LoadPanelAsset(R"(<PanelAsset id="P"><elements><element type="PanelProgress" id="p" bind="probe.nothing" textBind="probe.title"/></elements></PanelAsset>)");
        interpreter.Prepare(unreadable.asset);
        Settle();
        EXPECT_EQ(filledWidth(), 0.0f);
        EXPECT_EQ(textVertices(), kVerticesPerGlyph) << "a `?` over an empty bar";
    }

    TEST_F(PanelInterpreterTest, ATreeSectionOpensAndClosesIndentsItsChildrenAndWritesItsOpenBind) {
        Load(kTreeSection);
        const ImGuiID node = Chain(HostId(), {"settings", "Settings###settings"});
        const ImGuiID inside = Chain(HostId(), {"settings", "Settings###settings", "inside", "Inside"});
        EXPECT_FALSE(RecordOf(inside).has_value()) << "closed by `defaultOpen`";

        ClickAt(Locate(node));
        Settle();
        EXPECT_TRUE(probe.flag);
        const Optional<ImGuiItemRecord> header = RecordOf(node);
        const Optional<ImGuiItemRecord> child = RecordOf(inside);
        ASSERT_TRUE(header.has_value());
        ASSERT_TRUE(child.has_value()) << "an open node draws its children under its own id";
        EXPECT_NEAR(child->minX - header->minX, ImGui::GetStyle().IndentSpacing, 1.0f);

        ClickAt(Locate(node));
        Settle();
        EXPECT_FALSE(probe.flag);
        EXPECT_FALSE(RecordOf(inside).has_value());
    }

    TEST_F(PanelInterpreterTest, ADropdownNotesItsOpenCommandOncePerOpeningAndAClosingButtonClosesItAndIsEmitted) {
        Load(kSaveAsDropdown);
        const ImGuiID button = DropdownButtonId("bar", "saveAs", "Save as##saveAs");
        ClickAt(Locate(button));
        Settle();
        ASSERT_NE(DropdownPopup("bar", "saveAs"), nullptr);
        for (int i = 0; i < kSteadyFrames; ++i) {
            Frame();
        }
        EXPECT_EQ(TimesNoted("probe.opened"), 1);

        const ImGuiWindow* popup = DropdownPopup("bar", "saveAs");
        ASSERT_NE(popup, nullptr);
        ClickAt(LocateInWindow(Chain(popup->ID, {"save", "Save"}), *popup));
        Settle();
        EXPECT_EQ(DropdownPopup("bar", "saveAs"), nullptr);
        EXPECT_EQ(TimesNoted("probe.save"), 1);
        EXPECT_EQ(lastCommand, "probe.save");

        ClickAt(Locate(button));
        Settle();
        ASSERT_NE(DropdownPopup("bar", "saveAs"), nullptr);
        EXPECT_EQ(TimesNoted("probe.opened"), 2);
    }

    TEST_F(PanelInterpreterTest, ASteadyFrameOfAProgressAnOpenTreeAndAnOpenDropdownAllocatesNothing) {
        probe.gain = 0.5f;
        probe.title = "1 / 2 MB";
        Load(kProgress);
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });

        Load(kTreeSection);
        ClickAt(Locate(Chain(HostId(), {"settings", "Settings###settings"})));
        ASSERT_TRUE(probe.flag);
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });

        Load(kSaveAsDropdown);
        ClickAt(Locate(DropdownButtonId("bar", "saveAs", "Save as##saveAs")));
        Settle();
        ASSERT_NE(DropdownPopup("bar", "saveAs"), nullptr);
        for (int i = 0; i < kInputWarmUpFrames; ++i) {
            Frame();
        }
        ExpectNoAllocations([&] {
            for (int i = 0; i < kSteadyFrames; ++i) {
                Frame();
            }
        });
    }

}  // namespace BECore::Tests
