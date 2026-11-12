// What the UI loaders do with content that is larger, longer or odder than any real file: the string and theme tables here, the web front end in UIWebTest.cpp

#include <BECore/Config/XmlNode.h>
#include <BECore/UI/UIStrings.h>
#include <BECore/UI/UIThemes.h>
#include <BECore/UI/UITidyMessage.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// The problems a table gives for the text of a file, each of which must be one short line
        template <typename Table>
        eastl::vector<eastl::string> Load(Table& table, const std::string& xml) {
            pugi::xml_document document;
            EXPECT_TRUE(document.load_buffer(xml.data(), xml.size()));
            return table.LoadFromXml(XmlNode{document.document_element()});
        }

        void ExpectOneShortLine(const eastl::vector<eastl::string>& problems) {
            ASSERT_FALSE(problems.empty());
            for (const eastl::string& problem : problems) {
                EXPECT_LE(problem.size(), UIText::kMaxMessageBytes);
                EXPECT_EQ(problem.find('\n'), eastl::string::npos);
                EXPECT_EQ(problem.find('\t'), eastl::string::npos);
            }
        }

    }  // namespace

    TEST(UIContentLimitsTest, TheProblemsOfAStringTableAreShortLinesWhateverTheFileSays) {
        const std::string longKey = std::string(5000, 'k') + "}";
        const std::string xml = "<UIStrings default=\"" + std::string(2000, 'd') +
                                "\">"
                                "<language id=\"en\"><string key=\"" +
                                longKey + "\" text=\"x\"/><string key=\"a&#10;b\"/><other-" + std::string(3000, 'o') +
                                "/></language>"
                                "<language id=\"" +
                                std::string(1000, 'l') + "\"/><weird/></UIStrings>";
        UIStrings strings;
        ExpectOneShortLine(Load(strings, xml));
    }

    TEST(UIContentLimitsTest, TheProblemsOfAThemeTableAreShortLinesWhateverTheFileSays) {
        const std::string xml = "<UIThemes default=\"" + std::string(2000, 'd') +
                                "\">"
                                "<theme id=\"t\"><color name=\"" +
                                std::string(5000, 'n') + "\" value=\"#fff\"/><color name=\"a\" value=\"" + std::string(4000, 'v') + "&#10;\"/><color name=\"b&#9;c\" value=\"#fff\"/><x-" +
                                std::string(3000, 'x') + "/></theme><theme id=\"" + std::string(1000, 't') + "\"/></UIThemes>";
        UIThemes themes;
        ExpectOneShortLine(Load(themes, xml));
    }

    TEST(UIContentLimitsTest, AShortCleanMessageIsLeftAsItIs) {
        EXPECT_EQ(UIText::TidyMessage("a plain message"), "a plain message");
        EXPECT_EQ(UIText::TidyMessage("tab\tand\nline"), "tab and line");
        const std::string exact(UIText::kMaxMessageBytes, 'e');
        EXPECT_EQ(UIText::TidyMessage(eastl::string_view(exact.data(), exact.size())).size(), UIText::kMaxMessageBytes);
        const std::string over(UIText::kMaxMessageBytes + 1, 'e');
        const eastl::string cut = UIText::TidyMessage(eastl::string_view(over.data(), over.size()));
        EXPECT_EQ(cut.size(), UIText::kMaxMessageBytes);
        EXPECT_EQ(cut.substr(cut.size() - 3), "...");
        EXPECT_EQ(UIText::Excerpt(eastl::string_view(over.data(), over.size())).size(), UIText::kMaxExcerptBytes);
    }

}  // namespace BECore::Tests
