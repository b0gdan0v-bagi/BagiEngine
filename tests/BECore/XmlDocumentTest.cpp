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

}  // namespace BECore::Tests
