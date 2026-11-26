#include <BECore/File/File.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <Widgets/Export/ExportLog.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A log file as the export's process writes it: appended to while it is read
        class ExportLogTest : public ::testing::Test {
        protected:
            void SetUp() override {
                auto file = File::Open(_path, FileMode::Write);
                ASSERT_TRUE(file.has_value()) << file.error().c_str();
                _file = std::move(*file);
            }

            void Append(StringView text) {
                ASSERT_TRUE(_file.Write(text).has_value());
                ASSERT_TRUE(_file.Flush().has_value());
            }

            TestTempFolder _folder{"export_log"};
            String _path = _folder.PathOf("export.log");
            File _file;
        };

    }  // namespace

    TEST(ExportLineTest, TheMarksOfTheToolsDecideTheKind) {
        EXPECT_EQ(ClassifyExportLine("src/A.cpp:3:5: error: no member"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("A.cpp(3): error C2065: undeclared"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("A.obj : error LNK2019: unresolved"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("LINK : fatal error LNK1120"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("FAILED: bin/Game"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("CMake Error at CMakeLists.txt:3 (message):"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("== export failed: build failed with exit code 1"), ExportLineKind::Error);
        EXPECT_EQ(ClassifyExportLine("src/A.cpp:3:5: warning: unused variable [-Werror]"), ExportLineKind::Warning);
        EXPECT_EQ(ClassifyExportLine("A.cpp(3): warning C4100: unused"), ExportLineKind::Warning);
        EXPECT_EQ(ClassifyExportLine("CMake Warning (dev) at CMakeLists.txt:3:"), ExportLineKind::Warning);
        EXPECT_EQ(ClassifyExportLine("== step 2/3: build"), ExportLineKind::Step);
        EXPECT_EQ(ClassifyExportLine("== export ok: /games/error: here"), ExportLineKind::Step);
        EXPECT_EQ(ClassifyExportLine("[3/9] Building CXX object ErrorHandler.cpp.o"), ExportLineKind::Plain);
        EXPECT_EQ(ClassifyExportLine("$ cmake --build build"), ExportLineKind::Plain);
    }

    TEST_F(ExportLogTest, ALineIsThereOnceItsEndWasWritten) {
        ExportLog log;
        log.Follow(_path);
        Append("== step 1/3: configure\r\nfirst half");
        log.Read();
        ASSERT_EQ(log.GetLineCount(), 1u);
        EXPECT_EQ(log.GetLine(0).text, "== step 1/3: configure");
        const uint32_t revision = log.GetRevision();

        log.Read();
        EXPECT_EQ(log.GetRevision(), revision);

        // The first read ended at the end of the file: what comes later must still be read
        Append(", second half\nx: warning: w\ny: error: e\n");
        log.Read();
        ASSERT_EQ(log.GetLineCount(), 4u);
        EXPECT_EQ(log.GetLine(1).text, "first half, second half");
        EXPECT_NE(log.GetRevision(), revision);
        EXPECT_EQ(log.GetWarningCount(), 1u);
        EXPECT_EQ(log.GetErrorCount(), 1u);
        EXPECT_EQ(log.GetLine(3).kind, ExportLineKind::Error);
    }

    TEST_F(ExportLogTest, FinishTakesTheUnendedLineAndReadsNothingTwice) {
        ExportLog log;
        log.Follow(_path);
        Append("one\ntwo");
        log.Finish();
        ASSERT_EQ(log.GetLineCount(), 2u);
        EXPECT_EQ(log.GetLine(1).text, "two");
        log.Read();
        EXPECT_EQ(log.GetLineCount(), 2u);
    }

    TEST_F(ExportLogTest, TheResultLinesGiveTheFolderAndTheReason) {
        ExportLog log;
        log.Follow(_path);
        Append("== export ok: /my games/dist/Game-debug\r\n");
        log.Read();
        EXPECT_EQ(log.GetResultFolder(), "/my games/dist/Game-debug");
        EXPECT_TRUE(log.GetFailure().empty());

        log.Follow(_path);
        EXPECT_TRUE(log.GetResultFolder().empty());
        EXPECT_EQ(log.GetLineCount(), 0u);
        Append("== export failed: cmake is not on PATH\n");
        log.Read();
        EXPECT_EQ(log.GetFailure(), "cmake is not on PATH");
    }

    TEST_F(ExportLogTest, OnlyTheNewestLinesAreKeptAndEveryErrorIsCounted) {
        ExportLog log(3);
        log.Follow(_path);
        Append("a: error: 1\nb: error: 2\nc\nd\ne\n");
        log.Read();
        ASSERT_EQ(log.GetLineCount(), 3u);
        EXPECT_EQ(log.GetLine(0).text, "c");
        EXPECT_EQ(log.GetLine(2).text, "e");
        EXPECT_EQ(log.GetDroppedCount(), 2u);
        EXPECT_EQ(log.GetErrorCount(), 2u);
    }

    TEST_F(ExportLogTest, ALineWithoutAnEndIsCutAndTheNextOneIsWhole) {
        ExportLog log;
        log.Follow(_path);
        const String endless(kMaxExportLineBytes * 2, 'x');
        Append(endless);
        Append("\nnext\n");
        log.Read();
        ASSERT_EQ(log.GetLineCount(), 2u);
        EXPECT_EQ(log.GetLine(0).text.size(), kMaxExportLineBytes);
        EXPECT_EQ(log.GetLine(1).text, "next");
    }

    TEST_F(ExportLogTest, AFileThatIsNotThereYetIsReadWhenItAppears) {
        const String later = _folder.PathOf("later.log");
        ExportLog log;
        log.Follow(later);
        log.Read();
        EXPECT_EQ(log.GetLineCount(), 0u);
        _folder.Write("later.log", "here\n");
        log.Read();
        ASSERT_EQ(log.GetLineCount(), 1u);
        EXPECT_EQ(log.GetLine(0).text, "here");
    }

}  // namespace BECore::Tests
