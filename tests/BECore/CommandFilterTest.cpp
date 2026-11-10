#include <Common/Printers.h>
#include <Widgets/EditorLayout/CommandFilter.h>

namespace BECore::Tests {

    namespace {

        eastl::vector<EditorCommand> MakeCommands() {
            return {
                {PoolString::Intern("file.quit"), "Quit", std::nullopt, true},
                {PoolString::Intern("view.resetLayout"), "Reset Layout", std::nullopt, true},
                {PoolString::Intern("view.saveLayout"), "Save Layout As", std::nullopt, true},
                {PoolString::Intern("scene.save"), "Save Scene", std::nullopt, true},
                {PoolString::Intern("config.save"), "Save Config", std::nullopt, true},
                {PoolString::Intern("view.preferences"), "Preferences", std::nullopt, true},
            };
        }

        eastl::string Titles(const eastl::vector<const EditorCommand*>& found) {
            eastl::string text;
            for (const EditorCommand* command : found) {
                if (!text.empty()) {
                    text += "|";
                }
                text.append(command->title.data(), command->title.size());
            }
            return text;
        }

        eastl::string Filter(eastl::string_view query, const eastl::vector<EditorCommand>& commands) {
            return Titles(FilterCommands(query, {commands.data(), commands.size()}));
        }

    }  // namespace

    TEST(CommandFilterTest, EmptyQueryListsEverythingInTableOrder) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("", commands), "Quit|Reset Layout|Save Layout As|Save Scene|Save Config|Preferences");
        EXPECT_EQ(Filter("  ", commands), "Quit|Reset Layout|Save Layout As|Save Scene|Save Config|Preferences");
    }

    TEST(CommandFilterTest, MatchIsCaseInsensitiveSubsequence) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("QUIT", commands), "Quit");
        EXPECT_EQ(Filter("qt", commands), "Quit");
        EXPECT_EQ(Filter("zzz", commands), "");
        EXPECT_EQ(Filter("lay", commands), "Reset Layout|Save Layout As");
    }

    TEST(CommandFilterTest, StableOrderOnTies) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("save", commands), "Save Layout As|Save Scene|Save Config");
    }

    TEST(CommandFilterTest, PrefixAndWordStartBeatScatteredMatches) {
        const eastl::vector<EditorCommand> commands = {
            {PoolString::Intern("a.scatter"), "Slow Cardigan Yak", std::nullopt, true},
            {PoolString::Intern("b.word"), "Open Layout", std::nullopt, true},
            {PoolString::Intern("c.prefix"), "Layout Reset", std::nullopt, true},
            {PoolString::Intern("d.inside"), "Play Style", std::nullopt, true},
        };
        // The two scattered matches tie and keep the order of the table
        EXPECT_EQ(Filter("lay", commands), "Layout Reset|Open Layout|Slow Cardigan Yak|Play Style");
    }

    TEST(CommandFilterTest, WordStartInitialsFindACommand) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("sla", commands).substr(0, 14), "Save Layout As");
        EXPECT_EQ(Filter("scen", commands), "Save Scene");
    }

    TEST(CommandFilterTest, SpacesInTheQueryAreIgnored) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("save sc", commands), Filter("savesc", commands));
        EXPECT_EQ(Filter("save sc", commands).substr(0, 10), "Save Scene");
    }

    TEST(CommandFilterTest, IdIsMatchedToo) {
        const auto commands = MakeCommands();
        EXPECT_EQ(Filter("resetl", commands), "Reset Layout");
        EXPECT_EQ(Filter("config.s", commands), "Save Config");
    }

    TEST(CommandFilterTest, TitleMatchBeatsIdMatch) {
        const eastl::vector<EditorCommand> commands = {
            {PoolString::Intern("save.thing"), "Open", std::nullopt, true},
            {PoolString::Intern("x.y"), "Save It", std::nullopt, true},
        };
        EXPECT_EQ(Filter("save", commands), "Save It|Open");
    }

    TEST(CommandFilterTest, TheRealTableAnswersTheDocumentedQueries) {
        const auto commands = GetEditorCommands();
        const eastl::vector<const EditorCommand*> found = FilterCommands("lay", commands);
        ASSERT_GE(found.size(), 2u);
        EXPECT_EQ(found[0]->id, PoolString(EditorCommandIds::kViewResetLayout));
        EXPECT_EQ(found[1]->id, PoolString(EditorCommandIds::kViewSaveLayout));
        EXPECT_EQ(FilterCommands("", commands).size(), commands.size());
    }

}  // namespace BECore::Tests
