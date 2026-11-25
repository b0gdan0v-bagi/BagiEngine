#include <BECore/Widgets/PanelAssets/PanelAssetLimits.h>
#include <BECore/Widgets/PanelAssets/PanelAssetLoader.h>
#include <BECore/Widgets/PanelAssets/PanelElements.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

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

    TEST(PanelAssetLoadTest, BindingSyntaxIsChecked) {
        ExpectSingleIssue(R"(<element type="PanelText" id="t" bind="files"/>)", "t", "bind: expected 'source.path'");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" bind="!files.x"/>)", "t", "'!' is allowed in a condition only");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files.x["/>)", "t", "visible: bad path");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="1a.x"/>)", "t", "source name");
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x" visible="files."/>)", "t", "path after the source is empty");
        ExpectSingleIssue(R"(<element type="PanelButton" id="b" command="c.d" argument="a["/>)", "b", "argument: bad path");
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
        ExpectSingleIssue(R"(<element type="PanelText" id="t" text="x"><elements><element type="PanelRow" id="r"/></elements></element>)", "t", "only a section or a row holds elements");
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
