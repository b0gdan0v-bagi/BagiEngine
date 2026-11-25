#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr const char* kValidXml = R"(
<PanelAsset id="FileChanges" title="File changes" command="view.fileChanges" open="false" width="640" height="320">
  <sources>
    <source name="files" model="FileChangesPanelModel"/>
  </sources>
  <elements>
    <element type="PanelEmptyState" id="notRunning" icon="file-diff" text="The file watcher is not running" visible="!files.running"/>
    <element type="PanelRow" id="toolbar" visible="files.running">
      <elements>
        <element type="PanelButton" id="rescan" text="Rescan" icon="refresh-cw" command="fileChanges.rescan"/>
        <element type="PanelButton" id="applyAll" text="Apply all" command="fileChanges.applyAll" enableWhen="files.hasPending"/>
      </elements>
    </element>
    <element type="PanelTable" id="pending" source="files.pending" visible="files.hasPending">
      <columns>
        <column type="Text" id="path" field="path"/>
        <column type="Button" id="apply" icon="check" command="fileChanges.apply" argument="path"/>
      </columns>
    </element>
  </elements>
</PanelAsset>)";

        String Wrap(StringView elements) {
            return Format(R"(<PanelAsset id="Test"><elements>{}</elements></PanelAsset>)", elements);
        }

        String Joined(const Vector<PanelIssue>& issues) {
            String text;
            for (const PanelIssue& issue : issues) {
                text += Format("[{}] {}\n", issue.where, issue.message);
            }
            return text;
        }

        /// Whether some issue is at @p where and its message holds @p part
        bool HasIssue(const Vector<PanelIssue>& issues, StringView where, StringView part) {
            for (const PanelIssue& issue : issues) {
                if (issue.where == where && StringView(issue.message).find(part) != StringView::npos) {
                    return true;
                }
            }
            return false;
        }

        void ExpectSingleIssue(StringView elements, StringView where, StringView part) {
            const PanelLoadResult result = LoadPanelAsset(StringView(Wrap(elements)));
            EXPECT_EQ(result.issues.size(), 1u) << Joined(result.issues);
            EXPECT_TRUE(HasIssue(result.issues, where, part)) << "wanted [" << String(where).c_str() << "] ..." << String(part).c_str() << "...\n" << Joined(result.issues);
        }

        String Nested(size_t levels) {
            String open;
            String close;
            for (size_t i = 0; i < levels; ++i) {
                open += Format(R"(<element type="PanelRow" id="r{}"><elements>)", i);
                close += "</elements></element>";
            }
            return open + close;
        }

    }  // namespace

    TEST(PanelAssetLoadTest, ValidSampleHasNoIssue) {
        const PanelLoadResult result = LoadPanelAsset(kValidXml);

        EXPECT_TRUE(result.issues.empty()) << Joined(result.issues);
        ASSERT_NE(result.asset, nullptr);
        EXPECT_EQ(result.asset->elements.size(), 3u);
    }

    TEST(PanelAssetLoadTest, RequiredFieldsAreIssues) {
        ExpectSingleIssue(R"(<element type="PanelButton" id="b" text="x"/>)", "b", "command is required");
        ExpectSingleIssue(R"(<element type="PanelText" id="t"/>)", "t", "'text' or 'bind'");
        ExpectSingleIssue(R"(<element type="PanelField" id="f"/>)", "f", "bind is required");
        ExpectSingleIssue(R"(<element type="PanelSlot" id="s"/>)", "s", "name is required");
        ExpectSingleIssue(R"(<element type="PanelTable" id="t"><columns><column id="c" type="Button" command="a.b"/></columns></element>)", "t", "source is required");
    }

    TEST(PanelAssetLoadTest, ColumnsAreChecked) {
        const PanelLoadResult result = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelTable" id="t" source="a.b"><columns>
                 <column id="x" type="Button"/>
                 <column id="y" type="Text"/>
                 <column id="y" type="Text" field="p"/>
                 <column type="Text" field="p"/>
                 <column id="z" type="Text" field="a[1"/>
               </columns></element>)")));

        EXPECT_TRUE(HasIssue(result.issues, "t.x", "command is required")) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "t.y", "field is required")) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "t.y", "used twice")) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "t.columns[3]", "id must be")) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "t.z", "field: bad path")) << Joined(result.issues);
        EXPECT_EQ(result.issues.size(), 5u) << Joined(result.issues);
    }

    TEST(PanelAssetLoadTest, TheStyleFieldIsARelativePath) {
        const auto load = [](StringView styleField) {
            return LoadPanelAsset(
                StringView(Wrap(Format(R"(<element type="PanelTable" id="t" source="a.b"><columns><column id="x" type="Text" field="p" styleField="{}"/></columns></element>)", styleField))));
        };

        const PanelLoadResult good = load("style");
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);
        const PanelLoadResult nested = load("state.style");
        EXPECT_TRUE(nested.issues.empty()) << Joined(nested.issues);
        const PanelLoadResult bad = load("state[");
        EXPECT_EQ(bad.issues.size(), 1u) << Joined(bad.issues);
        EXPECT_TRUE(HasIssue(bad.issues, "t.x", "styleField: bad path")) << Joined(bad.issues);
    }

    TEST(PanelAssetLoadTest, AnInputColumnAndALabelledButtonColumnAreChecked) {
        const PanelLoadResult good = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelTable" id="t" source="a.b"><columns>
                 <column id="call" type="Button" field="label" command="c.d" argument="index" enableField="callable"/>
                 <column id="plain" type="Button" command="c.d"/>
                 <column id="args" type="Input" field="arguments" hint="h" enableField="hasParams"/>
               </columns></element>)")));
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        const PanelLoadResult bad = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelTable" id="t" source="a.b"><columns>
                 <column id="noField" type="Input"/>
                 <column id="badEnable" type="Input" field="a" enableField="b["/>
                 <column id="badLabel" type="Button" command="c.d" field="a["/>
               </columns></element>)")));
        EXPECT_TRUE(HasIssue(bad.issues, "t.noField", "field is required")) << Joined(bad.issues);
        EXPECT_TRUE(HasIssue(bad.issues, "t.badEnable", "enableField: bad path")) << Joined(bad.issues);
        EXPECT_TRUE(HasIssue(bad.issues, "t.badLabel", "field: bad path")) << Joined(bad.issues);
        EXPECT_EQ(bad.issues.size(), 3u) << Joined(bad.issues);
    }

    TEST(PanelAssetLoadTest, FieldsTakesAWholeObjectAndTheOthersNeedAPath) {
        const PanelLoadResult whole = LoadPanelAsset(StringView(Wrap(R"(<element type="PanelFields" id="f" bind="game"/>)")));
        EXPECT_TRUE(whole.issues.empty()) << Joined(whole.issues);
        const PanelLoadResult path = LoadPanelAsset(StringView(Wrap(R"(<element type="PanelFields" id="f" bind="game.fields"/>)")));
        EXPECT_TRUE(path.issues.empty()) << Joined(path.issues);

        ExpectSingleIssue(R"(<element type="PanelFields" id="f"/>)", "f", "bind is required");
        ExpectSingleIssue(R"(<element type="PanelFields" id="f" bind="!game"/>)", "f", "'!' is allowed in a condition only");
        ExpectSingleIssue(R"(<element type="PanelFields" id="f" bind="1game"/>)", "f", "source name");
        ExpectSingleIssue(R"(<element type="PanelField" id="f" bind="game"/>)", "f", "bind: expected 'source.path'");
    }

    TEST(PanelAssetLoadTest, ScrollRegionsAreBounded) {
        const PanelLoadResult atLimit = LoadPanelAsset(StringView(Wrap(Format(
            R"(<element type="PanelText" id="t" text="x" maxLines="{}"/><element type="PanelTable" id="r" source="a.b" scrollRows="{}"><columns><column id="c" type="Text" field="p"/></columns></element>)",
            PanelAssetLimits::kMaxScrollLines, PanelAssetLimits::kMaxScrollRows))));
        EXPECT_TRUE(atLimit.issues.empty()) << Joined(atLimit.issues);

        ExpectSingleIssue(StringView(Format(R"(<element type="PanelText" id="t" text="x" maxLines="{}"/>)", PanelAssetLimits::kMaxScrollLines + 1)), "t", "maxLines must be at most");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" maxLines="4000000000"/>)", "t", "maxLines must be at most");
        ExpectSingleIssue(StringView(Format(R"(<element type="PanelTable" id="r" source="a.b" scrollRows="{}"><columns><column id="c" type="Text" field="p"/></columns></element>)",
                                            PanelAssetLimits::kMaxScrollRows + 1)),
                          "r", "scrollRows must be at most");
    }

    TEST(PanelAssetLoadTest, BindingSyntaxIsChecked) {
        ExpectSingleIssue(R"(<element type="PanelText" id="t" bind="files"/>)", "t", "bind: expected 'source.path'");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" bind="!files.x"/>)", "t", "'!' is allowed in a condition only");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files.x["/>)", "t", "visible: bad path");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="1a.x"/>)", "t", "source name");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files."/>)", "t", "path after the source is empty");
    }

    TEST(PanelAssetLoadTest, AButtonArgumentIsALiteralAndItsWidthAnExtent) {
        // `a[` is no path, and a button has no row to read one from: it is only text
        const PanelLoadResult literal = LoadPanelAsset(StringView(Wrap(R"(<element type="PanelButton" id="b" command="c.d" argument="a[" width="9"/>)")));
        EXPECT_TRUE(literal.issues.empty()) << Joined(literal.issues);
        ExpectSingleIssue(R"(<element type="PanelButton" id="b" command="c.d" argument="a&#10;b"/>)", "b", "argument must not hold a control character");
        ExpectSingleIssue(R"(<element type="PanelButton" id="b" command="c.d" width="-1"/>)", "b", "width must be from 0 to");
        ExpectSingleIssue(StringView(Format(R"(<element type="PanelButton" id="b" command="c.d" argument="{}"/>)", String(PanelAssetLimits::kMaxTextBytes + 1, 'a'))), "b", "argument is longer than");
        ExpectSingleIssue(R"(<element type="PanelTable" id="t" source="a.b"><columns><column id="c" type="Button" command="c.d" argument="a["/></columns></element>)", "t.c", "argument: bad path");
    }

    TEST(PanelAssetLoadTest, ConditionExpressionsAreChecked) {
        const PanelLoadResult good = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelText" id="a" text="x" visible="files.running and files.count &gt;= 2 or files.mode == Busy"/>
               <element type="PanelButton" id="b" command="c.d" enableWhen="!files.a or files.b != 3"/>
               <element type="PanelPopup" id="p" open="files.n == 1 or files.n == 2"/>)")));
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files.a and"/>)", "t", "visible: a term is missing after 'and'");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files.a==1"/>)", "t", "visible: bad path");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files.mode &lt; Busy"/>)", "t", "visible: an order compare takes a number");
        ExpectSingleIssue(R"(<element type="PanelButton" id="b" command="c.d" enableWhen="!files.a == 1"/>)", "b", "enableWhen: '!' cannot be combined with a compare");
        ExpectSingleIssue(R"(<element type="PanelPopup" id="p" open="files.a or"/>)", "p", "open: a term is missing after 'or'");

        String terms = "files.a";
        for (size_t i = 0; i < PanelAssetLimits::kMaxConditionTerms; ++i) {
            terms += " and files.a";
        }
        ExpectSingleIssue(StringView(Format(R"(<element type="PanelText" id="t" text="x" visible="{}"/>)", terms)), "t", "more than 8 terms");
    }

    TEST(PanelAssetLoadTest, UnknownCommandAndModelNeedTheContext) {
        const String xml = R"(<PanelAsset id="T"><sources><source name="s" model="NoSuchModel"/></sources>
                                <elements><element type="PanelButton" id="b" command="no.such"/></elements></PanelAsset>)";
        EXPECT_TRUE(LoadPanelAsset(StringView(xml)).issues.empty());

        const auto knownCommand = [](StringView name) { return name == "view.fileChanges"; };
        const auto knownModel = [](StringView name) { return name == "FileChangesPanelModel"; };
        PanelValidationContext context;
        context.isKnownCommand = FunctionRef<bool(StringView)>(knownCommand);
        context.isKnownModel = FunctionRef<bool(StringView)>(knownModel);
        const PanelLoadResult result = LoadPanelAsset(StringView(xml), context);

        EXPECT_EQ(result.issues.size(), 2u) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "b", "unknown command 'no.such'")) << Joined(result.issues);
        EXPECT_TRUE(HasIssue(result.issues, "T", "unknown model 'NoSuchModel'")) << Joined(result.issues);
    }

    TEST(PanelAssetLoadTest, IdsAreWellFormedAndUnique) {
        ExpectSingleIssue(R"(<element type="PanelRow" id="a"/><element type="PanelRow" id="a"/>)", "a", "used twice");
        ExpectSingleIssue(R"(<element type="PanelRow" id="has space"/>)", "elements[0]", "id must be 1 to 64");
        ExpectSingleIssue(R"(<element type="PanelRow"/>)", "elements[0]", "id must be 1 to 64");
        const String longId(PanelAssetLimits::kMaxIdBytes + 1, 'a');
        ExpectSingleIssue(StringView(Format(R"(<element type="PanelRow" id="{}"/>)", longId)), "elements[0]", "id must be 1 to 64");
    }

    TEST(PanelAssetLoadTest, NestingAndElementCountAreLimited) {
        EXPECT_TRUE(LoadPanelAsset(StringView(Wrap(Nested(PanelAssetLimits::kMaxDepth)))).issues.empty());

        const PanelLoadResult deep = LoadPanelAsset(StringView(Wrap(Nested(PanelAssetLimits::kMaxDepth + 1))));
        EXPECT_EQ(deep.issues.size(), 1u) << Joined(deep.issues);
        ASSERT_FALSE(deep.issues.empty());
        EXPECT_NE(deep.issues[0].message.find("nested deeper than 16"), String::npos);

        String many;
        for (size_t i = 0; i < PanelAssetLimits::kMaxElements; ++i) {
            many += Format(R"(<element type="PanelSeparator" id="e{}"/>)", i);
        }
        EXPECT_TRUE(LoadPanelAsset(StringView(Wrap(many))).issues.empty());
        many += R"(<element type="PanelSeparator" id="last"/>)";
        const PanelLoadResult tooMany = LoadPanelAsset(StringView(Wrap(many)));
        ASSERT_EQ(tooMany.issues.size(), 1u) << Joined(tooMany.issues);
        EXPECT_NE(tooMany.issues[0].message.find("more than 1024 elements"), String::npos);
    }

    TEST(PanelAssetLoadTest, ColumnCountIsLimited) {
        String columns;
        for (size_t i = 0; i <= PanelAssetLimits::kMaxColumns; ++i) {
            columns += Format(R"(<column id="c{}" type="Text" field="f"/>)", i);
        }
        const PanelLoadResult result = LoadPanelAsset(StringView(Wrap(Format(R"(<element type="PanelTable" id="t" source="a.b"><columns>{}</columns></element>)", columns))));

        ASSERT_EQ(result.issues.size(), 1u) << Joined(result.issues);
        EXPECT_NE(result.issues[0].message.find("more than 128 columns"), String::npos);
    }

    TEST(PanelAssetLoadTest, OnlyContainersHoldElements) {
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x"><elements><element type="PanelRow" id="r"/></elements></element>)", "t",
                          "only a section, a row, a child, a popup, a tab bar or a tab holds elements");
    }

    TEST(PanelAssetLoadTest, ChildAndPopupHoldElements) {
        const PanelLoadResult clean = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelChild" id="left" width="14" height="6.5" border="true"><elements><element type="PanelText" id="a" text="x"/></elements></element>
               <element type="PanelPopup" id="p" title="T" open="!a.b"><elements><element type="PanelChild" id="inside"/></elements></element>
               <element type="PanelRow" id="r"><elements><element type="PanelChild" id="second"/></elements></element>)")));
        EXPECT_TRUE(clean.issues.empty()) << Joined(clean.issues);
    }

    TEST(PanelAssetLoadTest, ChildExtentIsBounded) {
        const auto child = [](StringView attributes) { return Format(R"(<element type="PanelChild" id="c" {}/>)", attributes); };

        const PanelLoadResult atLimit = LoadPanelAsset(StringView(Wrap(child(Format(R"(width="{}" height="0")", PanelAssetLimits::kMaxChildExtent)))));
        EXPECT_TRUE(atLimit.issues.empty()) << Joined(atLimit.issues);

        ExpectSingleIssue(child(R"(width="-1")"), "c", "width must be from 0");
        ExpectSingleIssue(child(Format(R"(height="{}")", PanelAssetLimits::kMaxChildExtent + 1.0f)), "c", "height must be from 0");

        // The reader refuses a text such as "nan", but an asset made in code goes through the validator alone
        auto asset = New<PanelAsset>();
        asset->id = "Test";
        const auto made = New<PanelChild>();
        made->id = "c";
        made->width = std::numeric_limits<float>::quiet_NaN();
        made->height = std::numeric_limits<float>::infinity();
        asset->elements.push_back(made);
        Vector<PanelIssue> issues;
        ValidatePanelAsset(*asset, {}, issues);
        EXPECT_TRUE(HasIssue(issues, "c", "width must be from 0")) << Joined(issues);
        EXPECT_TRUE(HasIssue(issues, "c", "height must be from 0")) << Joined(issues);
    }

    TEST(PanelAssetLoadTest, ControlsAreChecked) {
        const PanelLoadResult good = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelCheckbox" id="a" bind="s.on" text="On"/>
               <element type="PanelCheckbox" id="b" bind="s.on" labelBind="s.label" look="Toggle" style="Warning"/>
               <element type="PanelCombo" id="c" bind="s.mode" width="11"/>
               <element type="PanelCombo" id="d" bind="s.index" items="s.rows" itemField="name.text"/>
               <element type="PanelNumber" id="e" bind="s.n"/>
               <element type="PanelNumber" id="f" bind="s.n" text="ms" min="-5" max="5" step="0.5" slider="true" width="8"/>)")));
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        ExpectSingleIssue(R"(<element type="PanelCheckbox" id="k" text="x"/>)", "k", "bind is required");
        ExpectSingleIssue(R"(<element type="PanelCheckbox" id="k" bind="s.on"/>)", "k", "needs 'text' or 'labelBind'");
        ExpectSingleIssue(R"(<element type="PanelCheckbox" id="k" bind="s.on" labelBind="!s.l"/>)", "k", "'!' is allowed in a condition only");
        ExpectSingleIssue(R"(<element type="PanelCombo" id="k" items="s.rows"/>)", "k", "bind is required");
        ExpectSingleIssue(R"(<element type="PanelCombo" id="k" bind="s.mode" items="rows"/>)", "k", "items: expected 'source.path'");
        ExpectSingleIssue(R"(<element type="PanelCombo" id="k" bind="s.mode" itemField="x"/>)", "k", "itemField needs items");
        ExpectSingleIssue(R"(<element type="PanelCombo" id="k" bind="s.mode" items="s.rows" itemField="x["/>)", "k", "itemField: bad path");
        ExpectSingleIssue(R"(<element type="PanelCombo" id="k" bind="s.mode" width="-1"/>)", "k", "width must be from 0");
        ExpectSingleIssue(R"(<element type="PanelNumber" id="k"/>)", "k", "bind is required");
        ExpectSingleIssue(R"(<element type="PanelNumber" id="k" bind="s.n" width="257"/>)", "k", "width must be from 0");
    }

    TEST(PanelAssetLoadTest, ANumberRangeIsOrderedFiniteAndBounded) {
        const String limit = Format("{}", PanelAssetLimits::kMaxNumberExtent);
        const auto number = [](StringView attributes) { return Format(R"(<element type="PanelNumber" id="k" bind="s.n" {}/>)", attributes); };

        const PanelLoadResult atLimit = LoadPanelAsset(StringView(Wrap(number(Format(R"(min="-{0}" max="{0}" step="{0}")", limit)))));
        EXPECT_TRUE(atLimit.issues.empty()) << Joined(atLimit.issues);
        const PanelLoadResult unbounded = LoadPanelAsset(StringView(Wrap(number(R"(min="3" max="3")"))));
        EXPECT_TRUE(unbounded.issues.empty()) << "min equal to max leaves a drag unbounded: " << Joined(unbounded.issues).c_str();

        ExpectSingleIssue(number(R"(min="5" max="1")"), "k", "min must not be above max");
        ExpectSingleIssue(number(R"(slider="true")"), "k", "a slider needs min below max");
        ExpectSingleIssue(number(R"(slider="true" min="2" max="2")"), "k", "a slider needs min below max");
        ExpectSingleIssue(number(R"(min="-2000000000" max="0")"), "k", "min and max must be from");
        ExpectSingleIssue(number(R"(max="2000000000")"), "k", "min and max must be from");
        ExpectSingleIssue(number(R"(step="-1")"), "k", "step must be from 0");
        ExpectSingleIssue(number(R"(step="2000000000")"), "k", "step must be from 0");

        // The reader refuses a text such as "nan", but an asset made in code goes through the validator alone
        auto asset = New<PanelAsset>();
        asset->id = "Test";
        const auto made = New<PanelNumber>();
        made->id = "k";
        made->bind = "s.n";
        made->min = std::numeric_limits<float>::quiet_NaN();
        made->step = std::numeric_limits<float>::infinity();
        asset->elements.push_back(made);
        Vector<PanelIssue> issues;
        ValidatePanelAsset(*asset, {}, issues);
        EXPECT_TRUE(HasIssue(issues, "k", "min and max must be from")) << Joined(issues);
        EXPECT_TRUE(HasIssue(issues, "k", "step must be from 0")) << Joined(issues);
    }

    TEST(PanelAssetLoadTest, TheTitleBindingIsChecked) {
        const PanelLoadResult good = LoadPanelAsset(StringView(R"(<PanelAsset id="T" titleBind="console.title"><elements/></PanelAsset>)"));
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        const PanelLoadResult bad = LoadPanelAsset(StringView(R"(<PanelAsset id="T" titleBind="console"><elements/></PanelAsset>)"));
        EXPECT_TRUE(HasIssue(bad.issues, "T", "titleBind: expected 'source.path'")) << Joined(bad.issues);
        const PanelLoadResult negated = LoadPanelAsset(StringView(R"(<PanelAsset id="T" titleBind="!console.title"><elements/></PanelAsset>)"));
        EXPECT_TRUE(HasIssue(negated.issues, "T", "'!' is allowed in a condition only")) << Joined(negated.issues);
    }

    TEST(PanelAssetLoadTest, APopupNeedsAConditionAndNeverNests) {
        ExpectSingleIssue(R"(<element type="PanelPopup" id="p"/>)", "p", "open is required");
        ExpectSingleIssue(R"(<element type="PanelPopup" id="p" open="a"/>)", "p", "open: expected 'source.path'");
        ExpectSingleIssue(R"(<element type="PanelPopup" id="p" open="a.b"><elements><element type="PanelRow" id="r"><elements>
                                <element type="PanelPopup" id="q" open="a.c"/></elements></element></elements></element>)",
                          "q", "a popup is not nested in a popup");

        const PanelLoadResult siblings = LoadPanelAsset(
            StringView(Wrap(R"(<element type="PanelPopup" id="p" open="a.b"/><element type="PanelChild" id="c"><elements><element type="PanelPopup" id="q" open="a.c"/></elements></element>)")));
        EXPECT_TRUE(siblings.issues.empty()) << Joined(siblings.issues);
    }

    TEST(PanelAssetLoadTest, PopupsAreCounted) {
        String popups;
        for (size_t i = 0; i < PanelAssetLimits::kMaxPopups; ++i) {
            popups += Format(R"(<element type="PanelPopup" id="p{}" open="a.b"/>)", i);
        }
        const PanelLoadResult atLimit = LoadPanelAsset(StringView(Wrap(popups)));
        EXPECT_TRUE(atLimit.issues.empty()) << Joined(atLimit.issues);

        popups += R"(<element type="PanelPopup" id="extra" open="a.b"/><element type="PanelPopup" id="extra2" open="a.b"/>)";
        const PanelLoadResult over = LoadPanelAsset(StringView(Wrap(popups)));
        EXPECT_EQ(over.issues.size(), 1u) << Joined(over.issues);
        EXPECT_TRUE(HasIssue(over.issues, "extra", "more than 8 popups")) << Joined(over.issues);
    }

    TEST(PanelAssetLoadTest, ATabBarHoldsTabsAndATabLivesInABar) {
        const PanelLoadResult good = LoadPanelAsset(StringView(Wrap(
            R"(<element type="PanelTabs" id="bar"><elements>
                 <element type="PanelTab" id="one" title="One"><elements><element type="PanelText" id="a" text="x"/></elements></element>
                 <element type="PanelTab" id="two" title="Two"><elements><element type="PanelPopup" id="p" open="a.b"/></elements></element>
               </elements></element>)")));
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        ExpectSingleIssue(R"(<element type="PanelTabs" id="bar"><elements><element type="PanelTab" id="one" title="One"/><element type="PanelText" id="t" text="x"/></elements></element>)", "bar",
                          "a tab bar holds only tabs");
        ExpectSingleIssue(R"(<element type="PanelTab" id="lost" title="Lost"/>)", "lost", "a tab belongs in a tab bar");
        ExpectSingleIssue(R"(<element type="PanelRow" id="r"><elements><element type="PanelTab" id="lost" title="Lost"/></elements></element>)", "lost", "a tab belongs in a tab bar");
        ExpectSingleIssue(R"(<element type="PanelTabs" id="bar"/>)", "bar", "a tab bar needs tabs");
        ExpectSingleIssue(R"(<element type="PanelTabs" id="bar"><elements><element type="PanelTab" id="one"/></elements></element>)", "one", "title is required");
    }

    TEST(PanelAssetLoadTest, TabsAreCounted) {
        const auto bar = [](size_t first, size_t count) {
            String tabs;
            for (size_t i = 0; i < count; ++i) {
                tabs += Format(R"(<element type="PanelTab" id="t{}" title="T"/>)", first + i);
            }
            return Format(R"(<element type="PanelTabs" id="bar{}"><elements>{}</elements></element>)", first, tabs);
        };

        const PanelLoadResult atLimit = LoadPanelAsset(StringView(Wrap(bar(0, PanelAssetLimits::kMaxTabsPerBar))));
        EXPECT_TRUE(atLimit.issues.empty()) << Joined(atLimit.issues);

        const PanelLoadResult overBar = LoadPanelAsset(StringView(Wrap(bar(0, PanelAssetLimits::kMaxTabsPerBar + 1))));
        EXPECT_EQ(overBar.issues.size(), 1u) << Joined(overBar.issues);
        EXPECT_TRUE(HasIssue(overBar.issues, "bar0", "more than 16 tabs in a bar")) << Joined(overBar.issues);

        // Two full bars are the limit of the asset, a third bar's first tab is over it
        const size_t perBar = PanelAssetLimits::kMaxTabsPerBar;
        const PanelLoadResult twoBars = LoadPanelAsset(StringView(Wrap(bar(0, perBar) + bar(perBar, perBar))));
        EXPECT_TRUE(twoBars.issues.empty()) << Joined(twoBars.issues);
        const PanelLoadResult overAll = LoadPanelAsset(StringView(Wrap(bar(0, perBar) + bar(perBar, perBar) + bar(2 * perBar, 2))));
        EXPECT_EQ(overAll.issues.size(), 1u) << Joined(overAll.issues);
        EXPECT_TRUE(HasIssue(overAll.issues, Format("t{}", 2 * perBar), "more than 32 tabs in all")) << Joined(overAll.issues);
    }

    TEST(PanelAssetLoadTest, ADialogsWindowIsOpenAndNeedsNoCommand) {
        const auto load = [](StringView attributes) {
            return LoadPanelAsset(StringView(Format(R"(<PanelAsset id="Host" title="Host" {}><elements><element type="PanelPopup" id="p" open="a.b"/></elements></PanelAsset>)", attributes)));
        };

        const PanelLoadResult open = load(R"(window="Dialogs" open="true")");
        EXPECT_TRUE(open.issues.empty()) << Joined(open.issues);
        ASSERT_NE(open.asset, nullptr);
        EXPECT_EQ(open.asset->window, PanelWindowKind::Dialogs);
        EXPECT_TRUE(open.asset->command.empty());

        const PanelLoadResult closed = load(R"(window="Dialogs" open="false")");
        EXPECT_EQ(closed.issues.size(), 1u) << Joined(closed.issues);
        EXPECT_TRUE(HasIssue(closed.issues, "Host", "always open")) << Joined(closed.issues);

        const PanelLoadResult floating = load(R"(window="Floating")");
        EXPECT_TRUE(floating.issues.empty()) << Joined(floating.issues);
        const PanelLoadResult unknown = load(R"(window="Tabbed")");
        EXPECT_FALSE(unknown.issues.empty());
    }

    TEST(PanelAssetLoadTest, SelectedFieldIsARelativePathOfASelectableColumn) {
        const auto load = [](StringView column) { return LoadPanelAsset(StringView(Wrap(Format(R"(<element type="PanelTable" id="t" source="a.b"><columns>{}</columns></element>)", column)))); };

        const PanelLoadResult good = load(R"(<column id="x" type="Selectable" field="name" selectedField="state.chosen"/>)");
        EXPECT_TRUE(good.issues.empty()) << Joined(good.issues);

        const PanelLoadResult badPath = load(R"(<column id="x" type="Selectable" field="name" selectedField="chosen["/>)");
        EXPECT_EQ(badPath.issues.size(), 1u) << Joined(badPath.issues);
        EXPECT_TRUE(HasIssue(badPath.issues, "t.x", "selectedField: bad path")) << Joined(badPath.issues);

        const PanelLoadResult wrongType = load(R"(<column id="x" type="Text" field="name" selectedField="chosen"/>)");
        EXPECT_EQ(wrongType.issues.size(), 1u) << Joined(wrongType.issues);
        EXPECT_TRUE(HasIssue(wrongType.issues, "t.x", "selectedField is for a Selectable column")) << Joined(wrongType.issues);
    }

    TEST(PanelAssetLoadTest, ATextOverTheLimitIsAnIssue) {
        const String longText(PanelAssetLimits::kMaxTextBytes + 1, 'x');
        ExpectSingleIssue(StringView(Format(R"(<element type="PanelText" id="t" text="{}"/>)", longText)), "t", "text is longer than 512 bytes");
    }

    TEST(PanelAssetLoadTest, ReaderFindingsAreIssues) {
        const PanelLoadResult unknownAttribute = LoadPanelAsset(StringView(Wrap(R"(<element type="PanelButton" id="b" command="a.b" tooltips="x"/>)")));
        EXPECT_FALSE(unknownAttribute.issues.empty());
        EXPECT_NE(Joined(unknownAttribute.issues).find("did you mean 'tooltip'"), String::npos) << Joined(unknownAttribute.issues);

        EXPECT_FALSE(LoadPanelAsset(StringView(Wrap(R"(<element type="PanelHologram" id="h"/>)"))).issues.empty());
        EXPECT_FALSE(LoadPanelAsset(StringView(Wrap(R"(<element type="PanelInput" id="i" bind="a.b" width="wide"/>)"))).issues.empty());
        EXPECT_FALSE(LoadPanelAsset(StringView(Wrap(R"(<element type="PanelText" id="t" text="x" style="Loud"/>)"))).issues.empty());
    }

    TEST(PanelAssetLoadTest, AnElementWithoutATypeIsAnIssue) {
        ExpectSingleIssue(R"(<element id="y"/>)", "y", "type is required");
    }

    TEST(PanelAssetLoadTest, BadInputGivesIssuesAndNoCrash) {
        for (const char* text : {"", "   ", "not xml", "<PanelAsset", "<PanelAsset id=\"x\"><elements></PanelAsset>"}) {
            const PanelLoadResult result = LoadPanelAsset(text);
            EXPECT_FALSE(result.issues.empty()) << text;
        }

        const String huge(4 * 1024 * 1024 + 1, ' ');
        const PanelLoadResult tooBig = LoadPanelAsset(StringView(huge));
        EXPECT_EQ(tooBig.asset, nullptr);
        EXPECT_FALSE(tooBig.issues.empty());
    }

    TEST(PanelAssetLoadTest, TheSameTextGivesTheSameIssues) {
        const String xml = Wrap(R"(<element type="PanelText" id="t"/><element type="PanelButton" id="t"/>)");
        const PanelLoadResult first = LoadPanelAsset(StringView(xml));
        const PanelLoadResult second = LoadPanelAsset(StringView(xml));

        ASSERT_FALSE(first.issues.empty());
        EXPECT_EQ(Joined(first.issues), Joined(second.issues));
    }

    TEST(PanelAssetLoadTest, IssuesAreShortAndPrintable) {
        PanelAsset asset;
        asset.id = String(300, 'a') + "\x07";
        const auto row = New<PanelRow>();
        row->id = String("bad\x01id");
        asset.elements.push_back(row);
        Vector<PanelIssue> issues;
        ValidatePanelAsset(asset, {}, issues);

        ASSERT_FALSE(issues.empty());
        for (const PanelIssue& issue : issues) {
            EXPECT_LE(issue.where.size() + issue.message.size(), 256u);
            for (const char c : issue.where + issue.message) {
                EXPECT_GE(static_cast<unsigned char>(c), 0x20) << Joined(issues);
            }
        }
    }

    TEST(PanelAssetLoadTest, TheIssueListIsBounded) {
        String elements;
        for (size_t i = 0; i < 4 * PanelAssetLimits::kMaxIssues; ++i) {
            elements += Format(R"(<element type="PanelSlot" id="s{}"/>)", i);
        }
        const PanelLoadResult result = LoadPanelAsset(StringView(Wrap(elements)));

        EXPECT_EQ(result.issues.size(), PanelAssetLimits::kMaxIssues);
        EXPECT_NE(result.issues.back().message.find("more issues"), String::npos);
    }

}  // namespace BECore::Tests
