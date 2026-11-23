#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Widgets/EditorLayout/CommandFilter.h>

namespace BECore::Tests {

    namespace {

        Vector<EditorCommand> MakeCommands() {
            return {
                {"file.quit"_intern, "Quit", std::nullopt, true},
                {"view.resetLayout"_intern, "Reset Layout", std::nullopt, true},
                {"view.saveLayout"_intern, "Save Layout As", std::nullopt, true},
                {"scene.save"_intern, "Save Scene", std::nullopt, true},
                {"config.save"_intern, "Save Config", std::nullopt, true},
                {"view.preferences"_intern, "Preferences", std::nullopt, true},
            };
        }

        String Titles(const Vector<const EditorCommand*>& found) {
            String text;
            for (const EditorCommand* command : found) {
                if (!text.empty()) {
                    text += "|";
                }
                text.append(command->title.data(), command->title.size());
            }
            return text;
        }

        String Filter(StringView query, const Vector<EditorCommand>& commands) {
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
        const Vector<EditorCommand> commands = {
            {"a.scatter"_intern, "Slow Cardigan Yak", std::nullopt, true},
            {"b.word"_intern, "Open Layout", std::nullopt, true},
            {"c.prefix"_intern, "Layout Reset", std::nullopt, true},
            {"d.inside"_intern, "Play Style", std::nullopt, true},
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
        const Vector<EditorCommand> commands = {
            {"save.thing"_intern, "Open", std::nullopt, true},
            {"x.y"_intern, "Save It", std::nullopt, true},
        };
        EXPECT_EQ(Filter("save", commands), "Save It|Open");
    }

    TEST(CommandFilterTest, TheRealTableAnswersTheDocumentedQueries) {
        const auto commands = GetEditorCommands();
        const Vector<const EditorCommand*> found = FilterCommands("lay", commands);
        ASSERT_GE(found.size(), 2u);
        EXPECT_EQ(found[0]->id, PoolString(EditorCommandIds::kViewResetLayout));
        EXPECT_EQ(found[1]->id, PoolString(EditorCommandIds::kViewSaveLayout));
        EXPECT_EQ(FilterCommands("", commands).size(), commands.size());
    }

    TEST(CommandFilterTest, IntoGivesTheListOfFilterCommands) {
        const auto commands = MakeCommands();
        const Span<const EditorCommand> span{commands.data(), commands.size()};
        Vector<const EditorCommand*> out;
        for (const StringView query : {"", "  ", "lay", "sla", "resetl", "save", "zzz"}) {
            FilterCommandsInto(query, span, out);
            EXPECT_EQ(Titles(out), Titles(FilterCommands(query, span))) << "query: " << String(query.data(), query.size()).c_str();
        }
    }

    TEST(CommandFilterTest, EqualScoresKeepRegistrationOrderForManyCommands) {
        Vector<EditorCommand> commands;
        Vector<String> titles;
        titles.reserve(40);
        for (int i = 0; i < 40; ++i) {
            titles.push_back(Format("Save {}", i));
            commands.push_back({"x"_intern, StringView(titles.back().data(), titles.back().size()), std::nullopt, true});
        }
        const Span<const EditorCommand> span{commands.data(), commands.size()};
        const Vector<const EditorCommand*> found = FilterCommands("save", span);
        ASSERT_EQ(found.size(), commands.size());
        for (size_t i = 0; i < found.size(); ++i) {
            EXPECT_EQ(found[i], &commands[i]);
        }
    }

    TEST(CommandFilterTest, SecondCallIntoTheSameVectorAllocatesNothing) {
        const auto commands = MakeCommands();
        const Span<const EditorCommand> span{commands.data(), commands.size()};
        Vector<const EditorCommand*> out;
        FilterCommandsInto("save", span, out);
        ExpectNoAllocations([&] { FilterCommandsInto("lay", span, out); });
        EXPECT_EQ(Titles(out), "Reset Layout|Save Layout As");
    }

    TEST(CommandFilterTest, MatchListRefiltersOnlyWhenTheInputChanges) {
        const auto commands = MakeCommands();
        const Span<const EditorCommand> span{commands.data(), commands.size()};
        CommandMatchList list;
        EXPECT_TRUE(list.Update("lay", span));
        const String first = Titles(list.GetMatches());
        EXPECT_EQ(first, "Reset Layout|Save Layout As");

        EXPECT_FALSE(list.Update("lay", span));
        EXPECT_EQ(Titles(list.GetMatches()), first);

        EXPECT_TRUE(list.Update("quit", span));
        EXPECT_EQ(Titles(list.GetMatches()), "Quit");

        list.Invalidate();
        EXPECT_TRUE(list.Update("quit", span));
        EXPECT_FALSE(list.Update("quit", span));

        EXPECT_TRUE(list.Update("quit", span.subspan(0, 3)));
    }

    TEST(CommandFilterTest, MoveSelectionWrapsAndHandlesEmptyList) {
        EXPECT_EQ(MoveSelection(0, 1, 3), 1);
        EXPECT_EQ(MoveSelection(2, 1, 3), 0);
        EXPECT_EQ(MoveSelection(0, -1, 3), 2);
        EXPECT_EQ(MoveSelection(1, -1, 3), 0);
        EXPECT_EQ(MoveSelection(5, 1, 3), 0);
        EXPECT_EQ(MoveSelection(-4, 0, 3), 0);
        EXPECT_EQ(MoveSelection(0, 1, 0), 0);
        EXPECT_EQ(MoveSelection(7, -1, 0), 0);
    }

}  // namespace BECore::Tests
