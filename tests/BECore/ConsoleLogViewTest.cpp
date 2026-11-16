#include <Common/Printers.h>
#include <Widgets/Console/ConsoleLogView.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kTime = "12:34:56.789";
        constexpr StringView kChessFile = "a/src/Chess/Game/X.cpp";
        constexpr StringView kVulkanFile = "a/src/Modules/Vulkan/src/X.cpp";

        void AddLine(LogHistory& history, LogLevel level, StringView message, StringView file = {}) {
            const LogSite site{file.empty() ? nullptr : file.data(), file.empty() ? 0U : 1U};
            history.Add(level, message, site, kTime);
        }

        /// The messages of the rows, joined by '|'
        String Rows(const LogHistory& history, const ConsoleLogView& view) {
            String text;
            for (const uint32_t row : view.GetRows()) {
                if (!text.empty()) {
                    text += "|";
                }
                text += history.At(row).message;
            }
            return text;
        }

        uint16_t CategoryOf(const LogHistory& history, StringView name) {
            for (size_t i = 0; i < history.GetCategoryCount(); ++i) {
                if (history.GetCategoryName(i) == name) {
                    return static_cast<uint16_t>(i);
                }
            }
            return kMaxLogCategories;
        }

        String Title(const LogHistory& history) {
            String title;
            FormatConsoleTitle(history, title);
            return title;
        }

        LogHistory MakeHistory() {
            LogHistory history(16);
            AddLine(history, LogLevel::Debug, "Texture loaded", kVulkanFile);
            AddLine(history, LogLevel::Info, "Move played", kChessFile);
            AddLine(history, LogLevel::Warning, "texture missing", kVulkanFile);
            AddLine(history, LogLevel::Error, "Bad move", kChessFile);
            return history;
        }

    }  // namespace

    TEST(ConsoleLogViewTest, AnEmptyFilterListsEveryLine) {
        const LogHistory history = MakeHistory();
        ConsoleLogView view;
        view.Update(history, ConsoleLogFilter{}, false);
        EXPECT_EQ(Rows(history, view), "Texture loaded|Move played|texture missing|Bad move");
    }

    TEST(ConsoleLogViewTest, TheLevelMaskHidesTheLevelsThatAreOff) {
        const LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        filter.levelMask = LogLevelBit(LogLevel::Warning) | LogLevelBit(LogLevel::Error);
        ConsoleLogView view;
        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "texture missing|Bad move");

        filter.levelMask = 0;
        ++filter.revision;
        view.Update(history, filter, false);
        EXPECT_TRUE(view.GetRows().empty());
    }

    TEST(ConsoleLogViewTest, TheCategoryKeepsOnlyItsLines) {
        const LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        filter.category = CategoryOf(history, "Chess");
        ConsoleLogView view;
        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "Move played|Bad move");
    }

    TEST(ConsoleLogViewTest, TheTextIsACaseInsensitiveSubstring) {
        const LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        filter.text = "TEXTURE";
        ConsoleLogView view;
        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "Texture loaded|texture missing");

        filter.text = "no such text";
        ++filter.revision;
        view.Update(history, filter, false);
        EXPECT_TRUE(view.GetRows().empty());

        filter.text = "a text longer than any message of this history";
        ++filter.revision;
        view.Update(history, filter, false);
        EXPECT_TRUE(view.GetRows().empty());
    }

    TEST(ConsoleLogViewTest, TheThreeFiltersTogetherAllHaveToMatch) {
        const LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        filter.levelMask = LogLevelBit(LogLevel::Debug) | LogLevelBit(LogLevel::Warning);
        filter.category = CategoryOf(history, "Vulkan");
        filter.text = "missing";
        ConsoleLogView view;
        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "texture missing");

        filter.text = "move";
        ++filter.revision;
        view.Update(history, filter, false);
        EXPECT_TRUE(view.GetRows().empty());
    }

    TEST(ConsoleLogViewTest, AnUpdateWithoutAChangeDoesNotRebuild) {
        LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        ConsoleLogView view;
        view.Update(history, filter, false);
        view.Update(history, filter, false);
        EXPECT_EQ(view.GetRebuildCount(), 1U);

        AddLine(history, LogLevel::Info, "more");
        view.Update(history, filter, false);
        EXPECT_EQ(view.GetRebuildCount(), 2U);

        ++filter.revision;
        view.Update(history, filter, false);
        EXPECT_EQ(view.GetRebuildCount(), 3U);

        view.Update(history, filter, true);
        EXPECT_EQ(view.GetRebuildCount(), 4U);
        view.Update(history, filter, true);
        EXPECT_EQ(view.GetRebuildCount(), 4U);
    }

    TEST(ConsoleLogViewTest, APauseFreezesTheViewAndResumingShowsTheNewLines) {
        LogHistory history = MakeHistory();
        const ConsoleLogFilter filter;
        ConsoleLogView view;
        view.Update(history, filter, false);
        ASSERT_EQ(view.GetRows().size(), 4U);

        view.Update(history, filter, true);
        AddLine(history, LogLevel::Error, "while paused");
        view.Update(history, filter, true);
        EXPECT_EQ(Rows(history, view), "Texture loaded|Move played|texture missing|Bad move");

        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "Texture loaded|Move played|texture missing|Bad move|while paused");
    }

    TEST(ConsoleLogViewTest, AFilterChangeWhilePausedStaysInsideTheFrozenLines) {
        LogHistory history = MakeHistory();
        ConsoleLogFilter filter;
        ConsoleLogView view;
        view.Update(history, filter, true);
        AddLine(history, LogLevel::Error, "while paused");

        filter.levelMask = LogLevelBit(LogLevel::Error);
        ++filter.revision;
        view.Update(history, filter, true);
        EXPECT_EQ(Rows(history, view), "Bad move");
    }

    TEST(ConsoleLogViewTest, ClearingTheHistoryEmptiesTheRows) {
        LogHistory history = MakeHistory();
        const ConsoleLogFilter filter;
        ConsoleLogView view;
        view.Update(history, filter, false);
        ASSERT_FALSE(view.GetRows().empty());

        history.Clear();
        view.Update(history, filter, false);
        EXPECT_TRUE(view.GetRows().empty());

        view.Update(history, filter, true);
        AddLine(history, LogLevel::Info, "after the clear");
        view.Update(history, filter, true);
        EXPECT_TRUE(view.GetRows().empty());
    }

    TEST(ConsoleLogViewTest, TheRowsFollowAHistoryThatDroppedItsOldest) {
        LogHistory history(3);
        const ConsoleLogFilter filter;
        ConsoleLogView view;
        AddLine(history, LogLevel::Info, "one");
        AddLine(history, LogLevel::Info, "two");
        AddLine(history, LogLevel::Info, "three");
        view.Update(history, filter, false);
        AddLine(history, LogLevel::Info, "four");
        AddLine(history, LogLevel::Info, "five");
        view.Update(history, filter, false);
        EXPECT_EQ(Rows(history, view), "three|four|five");
        ASSERT_EQ(view.GetRows().size(), 3U);
        EXPECT_EQ(view.GetRows().front(), 0U);
        EXPECT_EQ(view.GetRows().back(), 2U);

        view.Update(history, filter, true);
        AddLine(history, LogLevel::Info, "six");
        view.Update(history, filter, true);
        EXPECT_EQ(Rows(history, view), "four|five");
    }

    TEST(ConsoleLogViewTest, TheTitleCountsErrorsAndWarnings) {
        LogHistory history(16);
        EXPECT_EQ(Title(history), "Console###Console");

        AddLine(history, LogLevel::Info, "info");
        AddLine(history, LogLevel::Debug, "debug");
        EXPECT_EQ(Title(history), "Console###Console");

        AddLine(history, LogLevel::Error, "e1");
        EXPECT_EQ(Title(history), "Console (1 error)###Console");
        AddLine(history, LogLevel::Error, "e2");
        AddLine(history, LogLevel::Error, "e3");
        EXPECT_EQ(Title(history), "Console (3 errors)###Console");

        for (int i = 0; i < 5; ++i) {
            AddLine(history, LogLevel::Warning, "w");
        }
        EXPECT_EQ(Title(history), "Console (3 errors, 5 warnings)###Console");
    }

    TEST(ConsoleLogViewTest, TheTitleLeavesOutALevelWithNoLinesAndCountsFatalAsAnError) {
        LogHistory history(16);
        AddLine(history, LogLevel::Warning, "w");
        EXPECT_EQ(Title(history), "Console (1 warning)###Console");

        AddLine(history, LogLevel::Fatal, "f");
        AddLine(history, LogLevel::Error, "e");
        EXPECT_EQ(Title(history), "Console (2 errors, 1 warning)###Console");
    }

    TEST(ConsoleLogViewTest, CompileTimeChecks) {
        static_assert(LogLevelBit(LogLevel::Debug) == 1U);
        static_assert((kAllLogLevels & LogLevelBit(LogLevel::Fatal)) != 0U);
        static_assert((kAllLogLevels >> EnumUtils<LogLevel>::Count()) == 0U);
    }

}  // namespace BECore::Tests
