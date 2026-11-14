#include <BECore/Reflection/XmlDeserializer.h>
#include <Games/GamesConfig.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// Reads @p xml (the text of a whole config file) into a GamesConfig and returns the errors in @p errors
        GamesConfig Load(StringView xml, Vector<DeserializeError>& errors) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            GamesConfig config;
            config.Deserialize(deserializer);
            errors = deserializer.GetErrors();
            return config;
        }

    }  // namespace

    TEST(GamesConfigTest, ReadsTheDefaultGame) {
        Vector<DeserializeError> errors;
        const auto config = Load(R"(<root><defaultGame name="Match3"/></root>)", errors);
        EXPECT_EQ(config._defaultGame._name.ToStringView(), "Match3");
        EXPECT_TRUE(errors.empty());
    }

    TEST(GamesConfigTest, NoDefaultGameLeavesTheNameEmpty) {
        Vector<DeserializeError> errors;
        const auto config = Load(R"(<root/>)", errors);
        EXPECT_TRUE(config._defaultGame._name.Empty());
        EXPECT_TRUE(errors.empty());
    }

    TEST(GamesConfigTest, ADefaultGameWithoutANameLeavesTheNameEmpty) {
        Vector<DeserializeError> errors;
        const auto config = Load(R"(<root><defaultGame/></root>)", errors);
        EXPECT_TRUE(config._defaultGame._name.Empty());
        EXPECT_TRUE(errors.empty());
    }

    TEST(GamesConfigTest, AnEmptyNameIsEmpty) {
        Vector<DeserializeError> errors;
        const auto config = Load(R"(<root><defaultGame name=""/></root>)", errors);
        EXPECT_TRUE(config._defaultGame._name.Empty());
    }

    TEST(GamesConfigTest, AnUnknownAttributeIsIgnored) {
        Vector<DeserializeError> errors;
        const auto config = Load(R"(<root level="3"><defaultGame name="Chess" colour="white"/><other/></root>)", errors);
        EXPECT_EQ(config._defaultGame._name.ToStringView(), "Chess");
        EXPECT_TRUE(errors.empty());
    }

}  // namespace BECore::Tests
