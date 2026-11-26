#include <BECore/Control/JsonTape.h>
#include <BECore/Control/JsonView.h>
#include <Common/Printers.h>
#include <Widgets/GamePanel/GameCommandForm.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        GameCommandForm FormOf(GameCommandParamType type, bool required) {
            const Array<GameCommandParam, 1> params{{{"value", type, required, ""}}};
            return GameCommandForm(Span<const GameCommandParam>(params.data(), params.size()));
        }

        void Type(GameCommandForm& form, size_t field, StringView text) {
            char* buffer = form.GetFields()[field].text;
            ASSERT_LE(text.size(), kMaxCommandFieldLength);
            buffer[text.copy(buffer, text.size())] = '\0';
        }

        /// The arguments of a form of one parameter holding @p text, or the form's error line
        String Built(GameCommandParamType type, bool required, StringView text) {
            GameCommandForm form = FormOf(type, required);
            Type(form, 0, text);
            const auto built = form.BuildArguments();
            return built ? *built : built.error();
        }

    }  // namespace

    TEST(GameCommandFormTest, EveryTypeBecomesAMemberInTheOrderOfTheParameters) {
        const Array<GameCommandParam, 5> params{{{"label", GameCommandParamType::String, true, ""},
                                                 {"seed", GameCommandParamType::Integer, true, ""},
                                                 {"speed", GameCommandParamType::Number, false, ""},
                                                 {"open", GameCommandParamType::Boolean, false, ""},
                                                 {"hands", GameCommandParamType::Json, false, ""}}};
        GameCommandForm form(Span<const GameCommandParam>(params.data(), params.size()));
        Type(form, 0, "east \"1\"");
        Type(form, 1, " -9223372036854775807 ");
        Type(form, 2, "1.5e3");
        form.GetFields()[3].flag = true;
        Type(form, 4, R"({"a":[1,null]})");

        const auto built = form.BuildArguments();

        ASSERT_TRUE(built.has_value()) << built.error().c_str();
        EXPECT_EQ(*built, R"({"label":"east \"1\"","seed": -9223372036854775807 ,"speed":1.5e3,"open":true,"hands":{"a":[1,null]}})");
        JsonTape tape;
        EXPECT_TRUE(tape.Parse(*built).has_value());
    }

    TEST(GameCommandFormTest, AnEmptyOptionalFieldIsLeftOutAndASwitchIsAlwaysWritten) {
        EXPECT_EQ(Built(GameCommandParamType::String, false, ""), "{}");
        EXPECT_EQ(Built(GameCommandParamType::Integer, false, "  "), "{}");
        EXPECT_EQ(Built(GameCommandParamType::Number, false, ""), "{}");
        EXPECT_EQ(Built(GameCommandParamType::Json, false, " \t"), "{}");
        EXPECT_EQ(Built(GameCommandParamType::Boolean, false, ""), R"({"value":false})");
    }

    TEST(GameCommandFormTest, SpacesAreAValueOfAString) {
        EXPECT_EQ(Built(GameCommandParamType::String, true, "  "), R"({"value":"  "})");
    }

    TEST(GameCommandFormTest, AnEmptyRequiredFieldIsAnErrorThatNamesIt) {
        EXPECT_EQ(Built(GameCommandParamType::String, true, ""), "'value' is required");
        EXPECT_EQ(Built(GameCommandParamType::Integer, true, " "), "'value' is required");
        EXPECT_EQ(Built(GameCommandParamType::Number, true, ""), "'value' is required");
        EXPECT_EQ(Built(GameCommandParamType::Json, true, ""), "'value' is required");
    }

    TEST(GameCommandFormTest, ATextThatIsNotAnIntegerIsRefused) {
        for (const StringView text : {"1.5", "1e3", "abc", "+1", "01", "1 2", "9223372036854775808", "\"1\"", "true"}) {
            EXPECT_EQ(Built(GameCommandParamType::Integer, false, text), "'value' has to be an integer") << String(text).c_str();
        }
    }

    TEST(GameCommandFormTest, ATextThatIsNotAFiniteNumberIsRefused) {
        for (const StringView text : {"nan", "inf", "1e999", "-1e999", "x", ".5", "1.", "[1]", "\"1\""}) {
            EXPECT_EQ(Built(GameCommandParamType::Number, false, text), "'value' has to be a finite number") << String(text).c_str();
        }
    }

    TEST(GameCommandFormTest, AJsonTextThatDoesNotParseIsAOneLineError) {
        const String deep(kMaxCommandFieldLength, '[');
        for (const StringView text : {StringView("{"), StringView("[1,]"), StringView("tru"), StringView("1 2"), StringView("\"\x01\""), StringView(deep.data(), deep.size())}) {
            const String built = Built(GameCommandParamType::Json, false, text);
            EXPECT_TRUE(StringView(built.data(), built.size()).starts_with("'value' is not a JSON value: ")) << built.c_str();
            EXPECT_EQ(built.find('\n'), String::npos);
        }
    }

    TEST(GameCommandFormTest, BytesThatAreNotUtf8StillMakeValidArguments) {
        GameCommandForm form = FormOf(GameCommandParamType::String, true);
        Type(form, 0, "a\xff\xfe");

        const auto built = form.BuildArguments();

        ASSERT_TRUE(built.has_value());
        JsonTape tape;
        EXPECT_TRUE(tape.Parse(*built).has_value()) << built->c_str();
    }

}  // namespace BECore::Tests
