#include <BECore/Config/XmlDocument.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/Reflection/ReaderLimits.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kWithComments = R"(<?xml version="1.0"?>
<!-- header -->
<root>
  <!-- first -->
  <sprites>
    <!-- inside -->
    <sprite name="A" />
  </sprites>
  <!-- last -->
</root>
)";

        constexpr StringView kByteOrderMark = "\xEF\xBB\xBF";

        class XmlDocumentTest : public ::testing::Test {
        protected:
            /// A path below the case's own folder, which the fixture clears first and removes after
            Path PathOf(const char* name) const {
                return Path(_folder.PathOf(name));
            }

            /// @p text loaded as a document and saved to the case's file, which is then read back
            String Saved(StringView text) const {
                XmlDocument document;
                EXPECT_TRUE(document.LoadFromString(text));
                EXPECT_TRUE(document.SaveToFile(_path));
                return ReadFile(_path);
            }

            TestTempFolder _folder{"XmlDocumentTest"};
            Path _path{PathOf("document.xml")};
        };

    }  // namespace

    TEST_F(XmlDocumentTest, TheRootIsTheElementAfterTheDeclarationAndTheComments) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(kWithComments));
        EXPECT_EQ(document.GetRoot().Name(), "root");
    }

    TEST_F(XmlDocumentTest, AFileKeepsItsDeclarationAndEveryComment) {
        WriteFile(_path, kWithComments);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        EXPECT_EQ(document.GetRoot().Name(), "root");

        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path), String(kWithComments));
    }

    TEST_F(XmlDocumentTest, ACanonicalTextIsSavedByteForByte) {
        EXPECT_EQ(Saved(kWithComments), String(kWithComments));
    }

    TEST_F(XmlDocumentTest, EveryAttributeOfAnElementIsOnOneLineAndNoDeclarationIsAdded) {
        constexpr StringView text = "<root>\n  <a x=\"1\" y=\"2\" z=\"3\" />\n</root>\n";
        EXPECT_EQ(Saved(text), String(text));
    }

    TEST_F(XmlDocumentTest, AByteOrderMarkIsKeptAndNotAddedToAFileWithoutOne) {
        constexpr StringView body = "<root>\n  <a />\n</root>\n";
        const String withMark = String(kByteOrderMark) + String(body);

        EXPECT_EQ(Saved(withMark), withMark);
        EXPECT_EQ(Saved(body), String(body));
    }

    TEST_F(XmlDocumentTest, AStringViewThatIsNotNulTerminatedLoadsByItsSize) {
        const StringView text = "<root /><junk";
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(text.substr(0, 8)));
        EXPECT_EQ(document.GetRoot().Name(), "root");
    }

    TEST_F(XmlDocumentTest, ClearForgetsTheByteOrderMark) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(String(kByteOrderMark) + "<root />"));
        document.Clear();
        ASSERT_TRUE(document.LoadFromString("<root />"));
        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path).find(kByteOrderMark.data()), String::npos);
    }

    TEST_F(XmlDocumentTest, AFileLoadedFromDiskKeepsItsByteOrderMarkAndGetsItBackOnSave) {
        const String withMark = String(kByteOrderMark) + "<root>\n  <a />\n</root>\n";
        WriteFile(_path, withMark);

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        EXPECT_EQ(document.GetRoot().Name(), "root");

        const Path copy = PathOf("copy.xml");
        ASSERT_TRUE(document.SaveToFile(copy));
        EXPECT_EQ(ReadFile(copy), withMark);
    }

    TEST_F(XmlDocumentTest, AFileThatCannotBeReadWholeDoesNotLoad) {
        const Path folder = PathOf("folder");
        MakeDirectory(folder);
        WriteFile(PathOf("empty.xml"), "");
        String tooBig = "<root><!--";
        tooBig.append(ReaderLimits::kMaxXmlDocumentBytes, 'a');
        tooBig += "--></root>";
        WriteFile(PathOf("big.xml"), tooBig);

        for (const char* name : {"missing.xml", "folder", "empty.xml", "big.xml"}) {
            XmlDocument document;
            EXPECT_FALSE(document.LoadFromFile(PathOf(name))) << name;
        }
    }

    TEST_F(XmlDocumentTest, ASaveCreatesTheMissingFoldersAndFailsOnAFolder) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root />"));
        const Path nested = PathOf("inner/deeper/a.xml");
        ASSERT_TRUE(document.SaveToFile(nested));
        EXPECT_TRUE(IsFile(nested));

        const Path folder = PathOf("folder");
        MakeDirectory(folder);
        EXPECT_FALSE(document.SaveToFile(folder));
    }

    TEST_F(XmlDocumentTest, AFailedSaveLeavesTheFileAsItWas) {
        WriteFile(_path, kWithComments);
        MakeDirectory(Path(Format("{}{}", _path.View(), kAtomicTempSuffix)));
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root />"));

        EXPECT_FALSE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path), String(kWithComments));
    }

    TEST_F(XmlDocumentTest, ASaveLeavesNoTemporaryFile) {
        Saved("<root />");
        EXPECT_FALSE(PathExists(Path(Format("{}{}", _path.View(), kAtomicTempSuffix))));
    }

#ifndef _WIN32
    TEST_F(XmlDocumentTest, ASaveThroughASymbolicLinkWritesTheLinkedFileAndKeepsTheLink) {
        const Path linked = PathOf("linked.xml");
        WriteFile(linked, "<old />\n");
        CreateSymlink(_path, linked);

        EXPECT_EQ(Saved("<root />\n"), "<root />\n");
        EXPECT_TRUE(IsSymbolicLink(_path));
        EXPECT_EQ(ReadFile(linked), "<root />\n");
    }

    TEST_F(XmlDocumentTest, ASaveToALinkThatLeadsNowhereFailsAndKeepsTheLink) {
        CreateSymlink(_path, PathOf("gone.xml"));
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root />"));

        EXPECT_FALSE(document.SaveToFile(_path));
        EXPECT_TRUE(IsSymbolicLink(_path));
        EXPECT_FALSE(PathExists(PathOf("gone.xml")));
    }
#endif

    TEST_F(XmlDocumentTest, ANodeIsFoundByANameThatIsNotNulTerminated) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(R"(<root ab="long" a="short"><ab /><a x="1" /></root>)"));
        const XmlNode root = document.GetRoot();
        const StringView name = StringView("ab").substr(0, 1);

        EXPECT_EQ(root.GetChild(name).GetAttribute("x"), Optional<StringView>("1"));
        EXPECT_EQ(root.GetAttribute(name), Optional<StringView>("short"));
        EXPECT_TRUE(root.GetChild("").IsEmpty());
        EXPECT_FALSE(root.GetAttribute("").has_value());
    }

    TEST_F(XmlDocumentTest, ASaveMakesAMissingFolderWithANonAsciiName) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root />"));
        const Path file = PathOf("caf\xC3\xA9") / "Conf\xC3\xA9.xml";
        ASSERT_TRUE(document.SaveToFile(file));

        XmlDocument loaded;
        ASSERT_TRUE(loaded.LoadFromFile(file));
        EXPECT_TRUE(static_cast<bool>(loaded.GetRoot()));
    }

    namespace {

        /// The shape of a hand-written TextureLibrary.xml: a blank line between the rows
        constexpr StringView kHandLaidLibrary = R"(<?xml version="1.0"?>
<!-- Sprite atlas -->
<root>
  <sprites>
    <sprite name="A" texturePath="a.png">
      <srcRect x="0" y="0" w="10" h="10" />
    </sprite>

    <sprite name="B" texturePath="a.png">
      <srcRect x="10" y="0" w="10" h="10" />
    </sprite>
  </sprites>
</root>
)";

        /// The shape of LoggerConfig.xml: a four-space indent and a blank line between the sinks
        constexpr StringView kFourSpaceConfig = R"(<?xml version="1.0" encoding="UTF-8"?>
<root>
    <!-- Logger Sinks Configuration -->
    <sinks>
        <sink type="OutputSink" priority="7" />

        <!-- File sink runs after Output -->
        <sink type="FileSink" priority="100" />
    </sinks>
</root>
)";

        /// The lines of @p text, a final line break not counted
        Vector<String> LinesOf(StringView text) {
            Vector<String> lines;
            size_t begin = 0;
            while (begin < text.size()) {
                size_t end = text.find('\n', begin);
                if (end == StringView::npos) {
                    end = text.size();
                }
                lines.push_back(String(text.substr(begin, end - begin)));
                begin = end + 1;
            }
            return lines;
        }

        /// Sets the write time of @p path far in the past, so that a write shows in the modification time at any clock resolution,
        /// and returns the time the file system reports
        int64_t SetLongAgo(const Path& path) {
            SetWriteTime(path, -std::chrono::hours(24 * 30));
            const Optional<FileInfo> info = GetFileInfo(path);
            return info ? info->modifyTimeNs : 0;
        }

    }  // namespace

    TEST_F(XmlDocumentTest, AHandLaidFileSavedUnchangedIsLeftAloneByteForByteAndInTime) {
        for (const StringView text : {kHandLaidLibrary, kFourSpaceConfig}) {
            WriteFile(_path, text);
            const int64_t before = SetLongAgo(_path);

            XmlDocument document;
            ASSERT_TRUE(document.LoadFromFile(_path));
            ASSERT_TRUE(document.SaveToFile(_path));

            EXPECT_EQ(ReadFile(_path), String(text));
            EXPECT_EQ(Require(GetFileInfo(_path)).modifyTimeNs, before);
        }
    }

    TEST_F(XmlDocumentTest, AnEditChangesOnlyTheLinesItTouches) {
        WriteFile(_path, kHandLaidLibrary);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));

        Vector<String> expected = LinesOf(kHandLaidLibrary);
        ASSERT_EQ(expected[4], "    <sprite name=\"A\" texturePath=\"a.png\">");
        expected[4] = "    <sprite name=\"Renamed\" texturePath=\"a.png\">";
        EXPECT_EQ(LinesOf(ReadFile(_path)), expected);
    }

    TEST_F(XmlDocumentTest, AFileBiggerThanTheScratchReadIsLaidOutLikeASmallOne) {
        // Blank lines of spaces between two rows, enough bytes to be read to the heap and few enough lines to stay under the merge's line limit
        constexpr size_t kPaddingLines = 17000;
        constexpr StringView kBlankLine = "               \n";
        String text = "<root>\n  <a x=\"1\" />\n";
        for (size_t i = 0; i < kPaddingLines; ++i) {
            text.append(kBlankLine.data(), kBlankLine.size());
        }
        text += "  <b x=\"2\" />\n</root>\n";
        WriteFile(_path, text);
        ASSERT_GT(text.size(), size_t{256} * 1024);

        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        document.GetRoot().GetPugiNode().first_child().attribute("x").set_value("9");
        ASSERT_TRUE(document.SaveToFile(_path));

        String expected = text;
        expected.replace(expected.find("x=\"1\""), 5, "x=\"9\"");
        EXPECT_EQ(ReadFile(_path), expected);
    }

    TEST_F(XmlDocumentTest, ANewElementKeepsTheBlankLinesAndTheIndentOfTheFile) {
        WriteFile(_path, kFourSpaceConfig);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        pugi::xml_node sinks = document.GetRoot().GetChild("sinks").GetPugiNode();
        sinks.append_child("sink").append_attribute("type").set_value("ConsoleSink");
        ASSERT_TRUE(document.SaveToFile(_path));

        Vector<String> expected = LinesOf(kFourSpaceConfig);
        expected.insert(expected.end() - 2, "    <sink type=\"ConsoleSink\" />");
        EXPECT_EQ(LinesOf(ReadFile(_path)), expected);
    }

    TEST_F(XmlDocumentTest, AFileThatCannotCarryTheLayoutIsRewrittenCanonicalAndThenStable) {
        constexpr StringView text = "<root>\n  <a x='1'/>\n  <b\n     y=\"2\" />\n</root>\n";
        WriteFile(_path, text);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        ASSERT_TRUE(document.SaveToFile(_path));

        const String canonical = ReadFile(_path);
        EXPECT_EQ(canonical, "<root>\n  <a x=\"1\" />\n  <b y=\"2\" />\n</root>\n");

        const int64_t before = SetLongAgo(_path);
        XmlDocument again;
        ASSERT_TRUE(again.LoadFromFile(_path));
        ASSERT_TRUE(again.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path), canonical);
        EXPECT_EQ(Require(GetFileInfo(_path)).modifyTimeNs, before);
    }

    TEST_F(XmlDocumentTest, AFileChangedOnDiskAfterTheLoadIsOverwrittenByTheDocument) {
        WriteFile(_path, kHandLaidLibrary);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));

        WriteFile(_path, "<root>\n  <other />\n</root>\n");
        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));

        const String saved = ReadFile(_path);
        EXPECT_EQ(saved.find("other"), String::npos);
        EXPECT_NE(saved.find("name=\"Renamed\""), String::npos);
    }

    TEST_F(XmlDocumentTest, AFileThatDoesNotParseIsReplacedByTheDocument) {
        WriteFile(_path, "<root><unclosed>");
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root>\n  <a />\n</root>\n"));
        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path), "<root>\n  <a />\n</root>\n");
    }

    TEST_F(XmlDocumentTest, TheByteOrderMarkSurvivesTheLayoutMerge) {
        const String withMark = String(kByteOrderMark) + String(kHandLaidLibrary);
        WriteFile(_path, withMark);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(_path), withMark);

        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));
        const String saved = ReadFile(_path);
        EXPECT_EQ(saved.substr(0, kByteOrderMark.size()), String(kByteOrderMark));
        EXPECT_NE(saved.find("\n\n    <sprite name=\"B\""), String::npos);
        EXPECT_NE(saved.find("name=\"Renamed\""), String::npos);
    }

}  // namespace BECore::Tests
