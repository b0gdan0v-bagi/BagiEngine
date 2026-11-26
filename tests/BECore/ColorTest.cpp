#include <BECore/Config/XmlDocument.h>
#include <BECore/Config/XmlNode.h>
#include <BECore/Math/Color.h>
#include <Common/Require.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    TEST(ColorTest, FourChannelsAreReadWithSpacesAroundThem) {
        const Color color = Require(Color::ParseColorFromString(" 0, 128 ,255,\t7 "));

        EXPECT_EQ(color.r, 0);
        EXPECT_EQ(color.g, 128);
        EXPECT_EQ(color.b, 255);
        EXPECT_EQ(color.a, 7);
    }

    TEST(ColorTest, AnyOtherTextIsNoColor) {
        for (const char* text : {"", " ", "1,2,3", "1,2,3,4,5", "1,2,3,4,", "1,,3,4", "1,2,3,300", "1,2,3,-1", "1,2,3,+4", "x,2,3,4", "12abc,2,3,4", "1.5,2,3,4", "1 2,3,4,5"}) {
            EXPECT_FALSE(Color::ParseColorFromString(text).has_value()) << '\'' << text << '\'';
        }
    }

    TEST(ColorTest, AnAttributeIsReadByTheSameRules) {
        XmlDocument document;
        ASSERT_TRUE(document.LoadFromString(R"(<root good="1, 2, 3, 4" bad="1,2,3,400" />)"));
        const XmlNode root = document.GetRoot();

        EXPECT_EQ(Require(root.ParseAttribute<Color>("good")).a, 4);
        EXPECT_FALSE(root.ParseAttribute<Color>("bad").has_value());
    }

}  // namespace BECore::Tests
