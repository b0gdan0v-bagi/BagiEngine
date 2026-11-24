#include <BECore/File/Path.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/TempPath.h>
#include <Common/TestFiles.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(XmlFileTest, AFileWithANonAsciiNameIsSavedAndReadBack) {
        TestTempFolder folder("XmlFileTest");
        const Path directory = Path(folder.PathOf("")) / "caf\xC3\xA9";
        ASSERT_TRUE(CreateDirectories(directory).has_value());
        const Path file = directory / "Conf\xC3\xA9.xml";

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("doc"));
        serializer.Write("value", int32_t{7});
        serializer.EndObject();
        ASSERT_TRUE(serializer.SaveToFile(file));

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromFile(file));
        ASSERT_TRUE(deserializer.BeginObject("doc"));
        int32_t value = 0;
        EXPECT_TRUE(deserializer.Read("value", value));
        EXPECT_EQ(value, 7);
    }

    TEST(XmlFileTest, AFileThatCannotBeParsedIsRefused) {
        TestTempFolder folder("XmlFileTest");
        const Path file(folder.Write("broken.xml", "<root><unclosed></root>"));

        XmlDeserializer deserializer;
        EXPECT_FALSE(deserializer.LoadFromFile(file));
        EXPECT_TRUE(deserializer.HasErrors());
    }

}  // namespace BECore::Tests
