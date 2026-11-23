#include <BECore/Config/StringXmlWriter.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        pugi::xml_document Parse(const char* text) {
            pugi::xml_document document;
            EXPECT_TRUE(document.load_string(text));
            return document;
        }

    }  // namespace

    TEST(StringXmlWriterTest, WritesAreAppendedInOrder) {
        StringXmlWriter writer;
        writer.write("<a>", 3);
        writer.write("xyz", 2);
        writer.write("</a>", 4);

        EXPECT_EQ(writer.text, "<a>xy</a>");
    }

    TEST(StringXmlWriterTest, DocumentSaveGivesTheIndentedTextWithTheDeclaration) {
        const pugi::xml_document document = Parse("<root><child a=\"1\"/><other/></root>");
        StringXmlWriter writer;
        document.save(writer, "  ");

        EXPECT_EQ(writer.text, "<?xml version=\"1.0\"?>\n"
                               "<root>\n"
                               "  <child a=\"1\" />\n"
                               "  <other />\n"
                               "</root>\n");
    }

    TEST(StringXmlWriterTest, NodePrintInRawFormatIsOneLineWithoutIndent) {
        const pugi::xml_document document = Parse("<root><child a=\"1\"><x/></child></root>");
        StringXmlWriter writer;
        document.document_element().child("child").print(writer, "", pugi::format_raw);

        EXPECT_EQ(writer.text, "<child a=\"1\"><x/></child>");
    }

    TEST(StringXmlWriterTest, NoDeclarationFlagLeavesOutTheDeclaration) {
        const pugi::xml_document document = Parse("<root/>");
        StringXmlWriter writer;
        document.save(writer, "  ", pugi::format_default | pugi::format_no_declaration);

        EXPECT_EQ(writer.text, "<root />\n");
    }

    TEST(StringXmlWriterTest, EmptyDocumentWithoutDeclarationIsAnEmptyText) {
        const pugi::xml_document document;
        StringXmlWriter writer;
        document.save(writer, "  ", pugi::format_no_declaration);

        EXPECT_TRUE(writer.text.empty());
    }

}  // namespace BECore::Tests
