#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <BECore/UI/UIBoxStyle.h>
#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIPanel.h>
#include <BECore/UI/UITextComponent.h>
#include <BECore/UI/UIThemes.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr Color Green{51, 170, 102, 255};
        constexpr Color DarkGreen{0, 90, 40, 255};

        eastl::string ElementToXml(const UIElement& element) {
            XmlSerializer serializer;
            EXPECT_TRUE(serializer.BeginObject("element"));
            element.Serialize(serializer);
            serializer.EndObject();
            return serializer.SaveToString();
        }

        /// The element of @p type that the attributes (and the @p body between its tags) make, and whether the deserializer reported an error
        IntrusivePtr<UIElement> Load(const char* type, const char* attributes, bool& hadErrors, const char* body = "") {
            const eastl::string xml = Format("<root><element type=\"{}\" {}>{}</element></root>", type, attributes, body);
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(StringView(xml)));
            auto element = AbstractFactory<UIElement>::GetInstance().Create(type);
            EXPECT_TRUE(deserializer.BeginObject("element"));
            element->Deserialize(deserializer);
            deserializer.EndObject();
            hadErrors = deserializer.HasErrors();
            return element;
        }

        class UIThemeColorTest : public ::testing::Test {
        protected:
            void TearDown() override {
                CoreManager::GetUIThemes().Clear();
            }
        };

    }  // namespace

    TEST_F(UIThemeColorTest, AColorTextIsALiteralOrAToken) {
        const auto literal = UIText::ParseColorRef(" #33AA66 ");
        ASSERT_TRUE(literal.has_value());
        EXPECT_TRUE(literal->token.Empty());
        EXPECT_TRUE(ColorsEqual(literal->color, Green));

        const auto token = UIText::ParseColorRef(" @primary-2 ");
        ASSERT_TRUE(token.has_value());
        EXPECT_EQ(token->token, "primary-2"_intern);
        EXPECT_EQ(token->color.a, 0) << "a token is transparent until its theme says otherwise";

        for (const char* bad : {"@", "@ primary", "@1st", "@a b", "@a.b", "@@a", "@a,b", "primary", "#12", "1,2,3"}) {
            EXPECT_FALSE(UIText::ParseColorRef(bad).has_value()) << bad;
        }
    }

    TEST_F(UIThemeColorTest, ATokenIsWrittenBackAsItsName) {
        EXPECT_EQ(UIText::FormatColorRef(Green, PoolString{}), "51,170,102,255");
        EXPECT_EQ(UIText::FormatColorRef(Green, "primary"_intern), "@primary") << "the channels of a token are not written";
        EXPECT_EQ(UIText::FormatColor(Green), "51,170,102,255") << "the literal-only form stays as it was";
    }

    TEST_F(UIThemeColorTest, AColorFieldReadsAndWritesAToken) {
        bool errors = false;
        auto element = Load("UIPanel", "color=\"@panel\" borderColor=\"@line\" spriteTint=\"1,2,3,4\"", errors);
        EXPECT_FALSE(errors);
        auto* panel = element->Cast<UIPanel>();
        ASSERT_NE(panel, nullptr);
        EXPECT_EQ(panel->color.token, "panel"_intern);
        EXPECT_EQ(panel->color.a, 0);
        EXPECT_EQ(panel->box.borderColor.token, "line"_intern);
        EXPECT_FALSE(panel->spriteTint.HasToken());
        EXPECT_EQ(panel->spriteTint.g, 2);

        const eastl::string xml = ElementToXml(*panel);
        EXPECT_NE(xml.find("color=\"@panel\""), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("borderColor=\"@line\""), eastl::string::npos) << "a token whose channels are transparent is still written: " << xml.c_str();
        EXPECT_NE(xml.find("spriteTint=\"1,2,3,4\""), eastl::string::npos) << xml.c_str();
    }

    TEST_F(UIThemeColorTest, ABadTokenIsReportedLikeABadColorAndTheOldValueKept) {
        for (const char* bad : {"color=\"@\"", "color=\"@1x\"", "color=\"@a b\"", "color=\"@x.y\""}) {
            bool errors = false;
            auto element = Load("UIPanel", bad, errors);
            EXPECT_TRUE(errors) << bad;
            EXPECT_FALSE(element->color.HasToken()) << bad;
        }
    }

    TEST_F(UIThemeColorTest, ALiteralReadAfterATokenDropsTheToken) {
        UIColor color = UIColor::FromToken("primary"_intern);
        ASSERT_TRUE(color.HasToken());

        color = Color{1, 2, 3, 4};

        EXPECT_FALSE(color.HasToken());
        EXPECT_EQ(color.b, 3);
    }

    TEST_F(UIThemeColorTest, TheColorOfAShadowAGlowAndTheTextEffectsMayBeATokenToo) {
        bool errors = false;
        auto element = Load("UIElement", "shadow=\"0,2,4,0,@shade\" glow=\"0,0,6,1,10,20,30,40\"", errors,
                            "<components><component type=\"UITextComponent\" textOutline=\"2,@edge\" textShadow=\"1,3,@drop\"/></components>");
        EXPECT_FALSE(errors);
        auto* text = element->GetComponent<UITextComponent>();
        ASSERT_NE(text, nullptr);

        EXPECT_EQ(element->box.shadowColorToken, "shade"_intern);
        EXPECT_EQ(element->box.shadow.color.a, 0);
        EXPECT_FLOAT_EQ(element->box.shadow.blur, 4.0f);
        EXPECT_TRUE(element->box.glowColorToken.Empty());
        EXPECT_EQ(element->box.glow.color.a, 40);
        EXPECT_EQ(text->textEffects.outlineColor.token, "edge"_intern);
        EXPECT_FLOAT_EQ(text->textEffects.outlineWidth, 2.0f);
        EXPECT_EQ(text->textEffects.shadowColor.token, "drop"_intern);
        EXPECT_FLOAT_EQ(text->textEffects.shadowOffsetY, 3.0f);

        const eastl::string xml = ElementToXml(*element);
        EXPECT_NE(xml.find("shadow=\"0,2,4,0,@shade\""), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("glow=\"0,0,6,1,#0A141E28\""), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("textOutline=\"2,@edge\""), eastl::string::npos) << xml.c_str();
        EXPECT_NE(xml.find("textShadow=\"1,3,@drop\""), eastl::string::npos) << xml.c_str();
    }

    TEST_F(UIThemeColorTest, ALiteralOverAShadowTokenDropsIt) {
        bool errors = false;
        auto element = Load("UIPanel", "shadow=\"0,2,4,0,@shade\"", errors);
        auto* panel = element->Cast<UIPanel>();
        ASSERT_NE(panel, nullptr);
        ASSERT_FALSE(panel->box.shadowColorToken.Empty());

        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><element shadow=\"0,2,4,0,#000000A0\"/></root>"));
        ASSERT_TRUE(deserializer.BeginObject("element"));
        panel->box.Deserialize(deserializer);
        deserializer.EndObject();

        EXPECT_TRUE(panel->box.shadowColorToken.Empty());
        EXPECT_EQ(panel->box.shadow.color.a, 0xA0);
    }

    TEST_F(UIThemeColorTest, AShadowOrATextEffectTokenIsRefusedByTheLiteralOnlyForms) {
        EXPECT_FALSE(UIText::ParseShadow("0,2,4,0,@shade").has_value());
        float width = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        Color color;
        EXPECT_FALSE(UIText::ParseTextOutline("2,@edge", width, color));
        EXPECT_FALSE(UIText::ParseTextShadow("1,3,@drop", x, y, color));

        PoolString token;
        EXPECT_TRUE(UIText::ParseTextOutline("2,@edge", width, color, &token));
        EXPECT_EQ(token, "edge"_intern);
        EXPECT_TRUE(UIText::ParseTextOutline("2,#000000", width, color, &token));
        EXPECT_TRUE(token.Empty()) << "a literal clears the token it is given";
    }

    TEST_F(UIThemeColorTest, TheBoxAndTheEffectsDifferByTheirTokens) {
        UIBoxStyle a;
        UIBoxStyle b;
        EXPECT_TRUE(a == b);

        a.borderColor = UIColor::FromToken("line"_intern);
        EXPECT_FALSE(a == b);
        b.borderColor = UIColor::FromToken("line"_intern);
        EXPECT_TRUE(a == b);
        a.shadowColorToken = "shade"_intern;
        EXPECT_FALSE(a == b);
        EXPECT_FALSE(a.HasEffect()) << "a token alone draws nothing until its colour is resolved";

        UITextEffects x;
        UITextEffects y;
        x.outlineColor = UIColor::FromToken("edge"_intern);
        EXPECT_FALSE(x == y);
        y.outlineColor = UIColor::FromToken("edge"_intern);
        EXPECT_TRUE(x == y);
    }

    TEST_F(UIThemeColorTest, ResolveWritesTheChannelsOfTheCurrentThemeAndKeepsTheToken) {
        UIThemes themes;
        themes.Set("light"_intern, "primary", Green);
        themes.Set("dark"_intern, "primary", DarkGreen);
        UIColor color = UIColor::FromToken("primary"_intern);

        themes.Resolve(color);
        EXPECT_TRUE(ColorsEqual(color, Green));
        EXPECT_EQ(color.token, "primary"_intern);

        themes.SetTheme("dark"_intern);
        themes.Resolve(color);
        EXPECT_TRUE(ColorsEqual(color, DarkGreen));
        EXPECT_EQ(color.token, "primary"_intern);
    }

    TEST_F(UIThemeColorTest, ALiteralAndAnEmptyTokenAreLeftAlone) {
        UIThemes themes;
        themes.Set("light"_intern, "primary", Green);
        UIColor literal(1, 2, 3, 4);
        UIColor empty;

        themes.Resolve(literal);
        themes.Resolve(empty);

        EXPECT_TRUE(ColorsEqual(literal, Color{1, 2, 3, 4}));
        EXPECT_TRUE(ColorsEqual(empty, Color{}));
        EXPECT_TRUE(themes.GetMissing().empty());
    }

    TEST_F(UIThemeColorTest, ATokenThatNoThemeDefinesIsMagentaAndRecordedOnce) {
        UIThemes themes;
        themes.Set("light"_intern, "primary", Green);
        UIColor first = UIColor::FromToken("typo"_intern);
        UIColor second = UIColor::FromToken("typo"_intern);
        UIColor other = UIColor::FromToken("other"_intern);

        themes.Resolve(first);
        themes.Resolve(second);
        themes.Resolve(other);
        themes.Resolve(first);

        EXPECT_TRUE(ColorsEqual(first, kMissingTokenColor));
        EXPECT_TRUE(ColorsEqual(second, kMissingTokenColor));
        EXPECT_EQ(first.token, "typo"_intern);
        ASSERT_EQ(themes.GetMissing().size(), 2u);
        EXPECT_EQ(themes.GetMissing()[0], "typo"_intern);
        EXPECT_EQ(themes.GetMissing()[1], "other"_intern);

        themes.Clear();
        EXPECT_TRUE(themes.GetMissing().empty());
    }

    TEST_F(UIThemeColorTest, ATokenOfTheDefaultThemeResolvesInAThemeThatLeavesItOut) {
        UIThemes themes;
        themes.Set("light"_intern, "primary", Green);
        themes.Set("light"_intern, "text", Color{9, 9, 9, 255});
        themes.Set("dark"_intern, "primary", DarkGreen);
        themes.SetTheme("dark"_intern);
        UIColor text = UIColor::FromToken("text"_intern);

        themes.Resolve(text);

        EXPECT_TRUE(ColorsEqual(text, Color{9, 9, 9, 255}));
        EXPECT_TRUE(themes.GetMissing().empty());
    }

    TEST_F(UIThemeColorTest, ATokenWhoseThemeColorIsInvisibleIsStillSavedAndCompared) {
        UIThemes themes;
        themes.Set("light"_intern, "none", Color{0, 0, 0, 0});
        UIBoxStyle box;
        box.highlight = UIColor::FromToken("none"_intern);
        themes.Resolve(box.highlight);

        XmlSerializer serializer;
        ASSERT_TRUE(serializer.BeginObject("element"));
        box.Serialize(serializer);
        serializer.EndObject();
        const eastl::string xml = serializer.SaveToString();

        EXPECT_NE(xml.find("highlight=\"@none\""), eastl::string::npos) << xml.c_str();
        EXPECT_FALSE(box.HasEffect());
    }

}  // namespace BECore::Tests
