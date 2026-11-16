#include <BECore/Logger/LogHistory.h>
#include <Common/Printers.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kTime = "12:34:56.789";

        void AddLine(LogHistory& history, LogLevel level, StringView message, StringView file = {}) {
            const LogSite site{file.empty() ? nullptr : file.data(), file.empty() ? 0U : 1U};
            history.Add(level, message, site, kTime);
        }

        String Name(const LogHistory& history, size_t index) {
            const StringView name = history.GetCategoryName(index);
            return String(name.data(), name.size());
        }

    }  // namespace

    TEST(LogHistoryTest, DropsTheOldestAtCapacity) {
        LogHistory history(3);
        AddLine(history, LogLevel::Info, "one");
        AddLine(history, LogLevel::Info, "two");
        AddLine(history, LogLevel::Info, "three");
        ASSERT_EQ(history.Size(), 3U);
        EXPECT_EQ(history.At(0).message, "one");

        AddLine(history, LogLevel::Info, "four");
        AddLine(history, LogLevel::Info, "five");
        ASSERT_EQ(history.Size(), 3U);
        EXPECT_EQ(history.At(0).message, "three");
        EXPECT_EQ(history.At(1).message, "four");
        EXPECT_EQ(history.At(2).message, "five");
    }

    TEST(LogHistoryTest, KeepsTheFieldsOfARecord) {
        LogHistory history(4);
        history.Add(LogLevel::Warning, "text", LogSite{"a/src/Chess/Game/X.cpp", 42}, kTime);
        ASSERT_EQ(history.Size(), 1U);
        const LogRecord& record = history.At(0);
        EXPECT_EQ(record.level, LogLevel::Warning);
        EXPECT_EQ(record.site.line, 42U);
        EXPECT_STREQ(record.time, "12:34:56.789");
        EXPECT_EQ(Name(history, record.category), "Chess");
    }

    TEST(LogHistoryTest, CountsFollowAddsDropsAndClear) {
        LogHistory history(3);
        AddLine(history, LogLevel::Error, "e1");
        AddLine(history, LogLevel::Warning, "w1");
        AddLine(history, LogLevel::Error, "e2");
        EXPECT_EQ(history.GetCount(LogLevel::Error), 2U);
        EXPECT_EQ(history.GetCount(LogLevel::Warning), 1U);

        AddLine(history, LogLevel::Info, "i1");  // drops e1
        EXPECT_EQ(history.GetCount(LogLevel::Error), 1U);
        EXPECT_EQ(history.GetCount(LogLevel::Info), 1U);

        AddLine(history, LogLevel::Info, "i2");  // drops w1
        EXPECT_EQ(history.GetCount(LogLevel::Warning), 0U);
        EXPECT_EQ(history.GetCount(LogLevel::Info), 2U);

        history.Clear();
        EXPECT_EQ(history.Size(), 0U);
        for (const LogLevel level : EnumUtils<LogLevel>::Values()) {
            EXPECT_EQ(history.GetCount(level), 0U);
        }
    }

    TEST(LogHistoryTest, TheSequenceStartsAtOneAndSurvivesClear) {
        LogHistory history(2);
        EXPECT_EQ(history.GetNextSequence(), 1U);
        AddLine(history, LogLevel::Info, "a");
        AddLine(history, LogLevel::Info, "b");
        AddLine(history, LogLevel::Info, "c");
        EXPECT_EQ(history.At(0).sequence, 2U);
        EXPECT_EQ(history.At(1).sequence, 3U);
        EXPECT_EQ(history.GetNextSequence(), 4U);

        history.Clear();
        EXPECT_EQ(history.GetNextSequence(), 4U);
        AddLine(history, LogLevel::Info, "d");
        EXPECT_EQ(history.At(0).sequence, 4U);
    }

    TEST(LogHistoryTest, TheVersionMovesOnAddAndClearOnly) {
        LogHistory history(2);
        const uint64_t start = history.GetVersion();
        EXPECT_EQ(history.GetVersion(), start);

        AddLine(history, LogLevel::Info, "a");
        const uint64_t afterAdd = history.GetVersion();
        EXPECT_GT(afterAdd, start);

        // Reads change nothing
        static_cast<void>(history.At(0));
        static_cast<void>(history.GetCount(LogLevel::Info));
        static_cast<void>(history.GetCategoryCount());
        EXPECT_EQ(history.GetVersion(), afterAdd);

        AddLine(history, LogLevel::Info, "b");
        AddLine(history, LogLevel::Info, "c");  // a drop is an add
        EXPECT_GT(history.GetVersion(), afterAdd);

        const uint64_t beforeClear = history.GetVersion();
        history.Clear();
        EXPECT_GT(history.GetVersion(), beforeClear);
    }

    TEST(LogHistoryTest, ACapacityOfOneKeepsTheLastLine) {
        LogHistory history(1);
        AddLine(history, LogLevel::Info, "a");
        AddLine(history, LogLevel::Error, "b");
        ASSERT_EQ(history.Size(), 1U);
        EXPECT_EQ(history.At(0).message, "b");
        EXPECT_EQ(history.GetCount(LogLevel::Info), 0U);
        EXPECT_EQ(history.GetCount(LogLevel::Error), 1U);

        LogHistory empty(0);
        EXPECT_EQ(empty.GetCapacity(), 1U);
    }

    TEST(LogHistoryTest, ALongTimeTextIsCut) {
        LogHistory history(1);
        history.Add(LogLevel::Info, "a", {}, "123456789012345678901234567890");
        EXPECT_EQ(StringView(history.At(0).time).size(), kLogTimeLength - 1);
    }

    TEST(LogHistoryTest, TheCategoryIsTheModuleFolderOfThePath) {
        EXPECT_EQ(LogCategoryOfFile("D:/x/engine/src/Modules/Vulkan/src/CoreVulkan/X.cpp"), "Vulkan");
        EXPECT_EQ(LogCategoryOfFile("src/Widgets/Console/X.cpp"), "Widgets");
        EXPECT_EQ(LogCategoryOfFile("D:\\a\\b\\src\\Chess\\Game\\X.cpp"), "Chess");
        EXPECT_EQ(LogCategoryOfFile("D:\\a\\b\\engine\\src\\Modules\\BECore\\Logger\\X.cpp"), "BECore");
        EXPECT_TRUE(LogCategoryOfFile("D:/a/b/Game/X.cpp").empty());
        EXPECT_TRUE(LogCategoryOfFile("src/X.cpp").empty());
        EXPECT_TRUE(LogCategoryOfFile("").empty());
    }

    TEST(LogHistoryTest, ALineWithoutAModuleIsGeneral) {
        LogHistory history(4);
        AddLine(history, LogLevel::Info, "no site");
        AddLine(history, LogLevel::Info, "no module", "/tmp/X.cpp");
        EXPECT_EQ(history.At(0).category, kGeneralLogCategory);
        EXPECT_EQ(history.At(1).category, kGeneralLogCategory);
        EXPECT_EQ(history.GetCategoryCount(), 1U);
        EXPECT_EQ(Name(history, 0), "General");
    }

    TEST(LogHistoryTest, CategoriesAreKeptOnceEach) {
        LogHistory history(8);
        AddLine(history, LogLevel::Info, "a", "src/Chess/X.cpp");
        AddLine(history, LogLevel::Info, "b", "src/Widgets/X.cpp");
        AddLine(history, LogLevel::Info, "c", "other/src/Chess/Y.cpp");
        ASSERT_EQ(history.GetCategoryCount(), 3U);
        EXPECT_EQ(history.At(0).category, history.At(2).category);
        EXPECT_NE(history.At(0).category, history.At(1).category);

        // Clear keeps the categories, so a record keeps meaning the same name
        history.Clear();
        EXPECT_EQ(history.GetCategoryCount(), 3U);
    }

    TEST(LogHistoryTest, TheCategoriesStopAtTheCapAndShareOther) {
        LogHistory history(kMaxLogCategories * 3);
        for (int i = 0; i < kMaxLogCategories * 2; ++i) {
            // `file` outlives only the call: the history reads the site while adding and the test never reads it back
            const String file = Format("src/Mod{}/X.cpp", i);
            AddLine(history, LogLevel::Info, "x", StringView(file.data(), file.size()));
        }
        EXPECT_EQ(history.GetCategoryCount(), size_t{kMaxLogCategories});
        EXPECT_EQ(Name(history, kMaxLogCategories - 1), "Other");

        const LogRecord& first = history.At(0);
        const LogRecord& last = history.At(history.Size() - 1);
        EXPECT_EQ(Name(history, first.category), "Mod0");
        EXPECT_EQ(last.category, uint16_t{kMaxLogCategories - 1});
    }

}  // namespace BECore::Tests
