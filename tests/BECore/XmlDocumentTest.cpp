#include <BECore/Config/XmlDocument.h>
#include <BECore/Config/XmlNode.h>
#include <Common/Printers.h>
#include <Common/TempPath.h>
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
            void SetUp() override {
                _path = TestTempPath("XmlDocumentTest_" + String(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".xml");
            }

            void TearDown() override {
                std::error_code error;
                std::filesystem::remove(_path, error);
            }

            String Saved(StringView text) {
                XmlDocument document;
                EXPECT_TRUE(document.LoadFromString(text));
                EXPECT_TRUE(document.SaveToFile(_path));
                return ReadFile();
            }

            String ReadFile() const {
                std::ifstream in(_path, std::ios::in | std::ios::binary);  //-NoStd: the test reads its own temporary file byte for byte
                String bytes;
                bytes.resize(static_cast<size_t>(std::filesystem::file_size(_path)));
                in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                return bytes;
            }

            void WriteFile(StringView bytes) const {
                std::ofstream out(_path, std::ios::out | std::ios::binary);  //-NoStd: the test writes its own temporary file byte for byte
                out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }

            std::filesystem::path _path;
        };

    }  // namespace

    TEST_F(XmlDocumentTest, TheRootIsTheElementAfterTheDeclarationAndTheComments) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(kWithComments));
        EXPECT_EQ(document.GetRoot().Name(), "root");
    }

    TEST_F(XmlDocumentTest, AFileKeepsItsDeclarationAndEveryComment) {
        WriteFile(kWithComments);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        EXPECT_EQ(document.GetRoot().Name(), "root");

        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(), String(kWithComments));
    }

    TEST_F(XmlDocumentTest, ACanonicalTextIsSavedByteForByte) {
        EXPECT_EQ(Saved(kWithComments), String(kWithComments));
    }

    TEST_F(XmlDocumentTest, EveryAttributeOfAnElementIsOnOneLine) {
        constexpr StringView text = "<root>\n  <a x=\"1\" y=\"2\" z=\"3\" />\n</root>\n";
        EXPECT_EQ(Saved(text), String(text));
    }

    TEST_F(XmlDocumentTest, ATextWithoutADeclarationIsSavedWithoutOne) {
        constexpr StringView text = "<root>\n  <a />\n</root>\n";
        const String saved = Saved(text);
        EXPECT_EQ(saved.find("<?xml"), String::npos);
        EXPECT_EQ(saved, String(text));
    }

    TEST_F(XmlDocumentTest, AByteOrderMarkIsKeptAndNotAddedToAFileWithoutOne) {
        constexpr StringView body = "<root>\n  <a />\n</root>\n";
        const String withMark = String(kByteOrderMark) + String(body);

        EXPECT_EQ(Saved(withMark), withMark);
        EXPECT_EQ(Saved(body), String(body));
    }

    TEST_F(XmlDocumentTest, TheByteOrderMarkIsNotPartOfTheDocument) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(String(kByteOrderMark) + "<root />"));
        EXPECT_EQ(document.GetRoot().Name(), "root");
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
        EXPECT_EQ(ReadFile().find(kByteOrderMark.data()), String::npos);
    }

    TEST_F(XmlDocumentTest, ASaveCreatesTheMissingFoldersAndFailsOnAFolder) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root />"));
        std::filesystem::path folder = _path;
        folder += "_dir";
        const std::filesystem::path nested = folder / "inner" / "a.xml";
        ASSERT_TRUE(document.SaveToFile(nested));
        EXPECT_TRUE(std::filesystem::exists(nested));
        std::error_code error;
        std::filesystem::remove_all(folder, error);

        EXPECT_FALSE(document.SaveToFile(std::filesystem::temp_directory_path()));
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

        /// A time far from now, so that a write shows in the modification time at any clock resolution
        std::filesystem::file_time_type LongAgo() {
            return std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 30);
        }

    }  // namespace

    TEST_F(XmlDocumentTest, AHandLaidFileSavedUnchangedIsLeftAloneByteForByteAndInTime) {
        for (const StringView text : {kHandLaidLibrary, kFourSpaceConfig}) {
            WriteFile(text);
            const auto before = LongAgo();
            std::filesystem::last_write_time(_path, before);

            XmlDocument document;
            ASSERT_TRUE(document.LoadFromFile(_path));
            ASSERT_TRUE(document.SaveToFile(_path));

            EXPECT_EQ(ReadFile(), String(text));
            EXPECT_EQ(std::filesystem::last_write_time(_path), before);
        }
    }

    TEST_F(XmlDocumentTest, AnEditChangesOnlyTheLinesItTouches) {
        WriteFile(kHandLaidLibrary);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));

        Vector<String> expected = LinesOf(kHandLaidLibrary);
        ASSERT_EQ(expected[4], "    <sprite name=\"A\" texturePath=\"a.png\">");
        expected[4] = "    <sprite name=\"Renamed\" texturePath=\"a.png\">";
        EXPECT_EQ(LinesOf(ReadFile()), expected);
    }

    TEST_F(XmlDocumentTest, ANewElementKeepsTheBlankLinesAndTheIndentOfTheFile) {
        WriteFile(kFourSpaceConfig);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        pugi::xml_node sinks = document.GetRoot().GetChild("sinks").GetPugiNode();
        sinks.append_child("sink").append_attribute("type").set_value("ConsoleSink");
        ASSERT_TRUE(document.SaveToFile(_path));

        Vector<String> expected = LinesOf(kFourSpaceConfig);
        expected.insert(expected.end() - 2, "    <sink type=\"ConsoleSink\" />");
        EXPECT_EQ(LinesOf(ReadFile()), expected);
    }

    TEST_F(XmlDocumentTest, AFileThatCannotCarryTheLayoutIsRewrittenCanonicalAndThenStable) {
        constexpr StringView text = "<root>\n  <a x='1'/>\n  <b\n     y=\"2\" />\n</root>\n";
        WriteFile(text);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        ASSERT_TRUE(document.SaveToFile(_path));

        const String canonical = ReadFile();
        EXPECT_EQ(canonical, "<root>\n  <a x=\"1\" />\n  <b y=\"2\" />\n</root>\n");

        const auto before = LongAgo();
        std::filesystem::last_write_time(_path, before);
        XmlDocument again;
        ASSERT_TRUE(again.LoadFromFile(_path));
        ASSERT_TRUE(again.SaveToFile(_path));
        EXPECT_EQ(ReadFile(), canonical);
        EXPECT_EQ(std::filesystem::last_write_time(_path), before);
    }

    TEST_F(XmlDocumentTest, AFileChangedOnDiskAfterTheLoadIsOverwrittenByTheDocument) {
        WriteFile(kHandLaidLibrary);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));

        WriteFile("<root>\n  <other />\n</root>\n");
        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));

        const String saved = ReadFile();
        EXPECT_EQ(saved.find("other"), String::npos);
        EXPECT_NE(saved.find("name=\"Renamed\""), String::npos);
    }

    TEST_F(XmlDocumentTest, AFileThatDoesNotParseIsReplacedByTheDocument) {
        WriteFile("<root><unclosed>");
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString("<root>\n  <a />\n</root>\n"));
        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(), "<root>\n  <a />\n</root>\n");
    }

    TEST_F(XmlDocumentTest, TheByteOrderMarkSurvivesTheLayoutMerge) {
        const String withMark = String(kByteOrderMark) + String(kHandLaidLibrary);
        WriteFile(withMark);
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromFile(_path));
        ASSERT_TRUE(document.SaveToFile(_path));
        EXPECT_EQ(ReadFile(), withMark);

        document.GetRoot().GetChild("sprites").GetPugiNode().first_child().attribute("name").set_value("Renamed");
        ASSERT_TRUE(document.SaveToFile(_path));
        const String saved = ReadFile();
        EXPECT_EQ(saved.substr(0, kByteOrderMark.size()), String(kByteOrderMark));
        EXPECT_NE(saved.find("\n\n    <sprite name=\"B\""), String::npos);
        EXPECT_NE(saved.find("name=\"Renamed\""), String::npos);
    }

}  // namespace BECore::Tests
