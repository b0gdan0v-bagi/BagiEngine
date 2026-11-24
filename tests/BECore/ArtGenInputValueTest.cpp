#include <BECore/Control/JsonWriter.h>
#include <Common/JsonDoc.h>
#include <Common/Printers.h>
#include <Widgets/ArtGen/ArtGenInputValue.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        ArtGenInputValue ReadOf(StringView json) {
            return ArtGenInputValue::Read(JsonDoc(json));
        }

        String WrittenText(const ArtGenInputValue& value) {
            String text;
            JsonWriter writer(text);
            value.Write(writer);
            return text;
        }

        String Shown(const ArtGenInputValue& value) {
            char text[64];
            value.ToText(text, sizeof(text));
            return String(text);
        }

    }  // namespace

    TEST(ArtGenInputValueTest, EveryKindOfJsonValueIsRead) {
        EXPECT_EQ(ReadOf("null").kind, ArtGenInputKind::None);
        EXPECT_EQ(ReadOf("true"), ArtGenInputValue::FromBool(true));
        EXPECT_EQ(ReadOf("-7"), ArtGenInputValue::FromInteger(-7));
        EXPECT_EQ(ReadOf("2.5"), ArtGenInputValue::FromNumber(2.5));
        EXPECT_EQ(ReadOf(R"("aé\n")"), ArtGenInputValue::FromText("a\xC3\xA9\n")) << "an escape is decoded";
        EXPECT_EQ(ReadOf("[2,{\"b\":1,\"a\":0}]").kind, ArtGenInputKind::Other);
        EXPECT_EQ(ReadOf("[2,{\"b\":1,\"a\":0}]").text, R"([2,{"a":0,"b":1}])") << "kept compact with the keys in byte order";
        EXPECT_EQ(ReadOf("18446744073709551615").kind, ArtGenInputKind::Other) << "an integer past int64 is kept as its text";
        EXPECT_EQ(ArtGenInputValue::Read(JsonView()).kind, ArtGenInputKind::None) << "a value that is not there is none";
    }

    TEST(ArtGenInputValueTest, AWrittenValueReadsBackTheSame) {
        for (const StringView json : {StringView("null"), StringView("false"), StringView("12"), StringView("0.125"), StringView(R"("two words")"), StringView(R"(["x",[1,2],{"k":null}])")}) {
            const ArtGenInputValue value = ReadOf(json);
            const String written = WrittenText(value);
            EXPECT_EQ(ReadOf(StringView(written)), value) << String(json).c_str();
        }
        EXPECT_EQ(WrittenText(ArtGenInputValue{}), "null");
        EXPECT_EQ(WrittenText(ArtGenInputValue::FromNumber(1.0)), "1.0");
        EXPECT_EQ(WrittenText(ArtGenInputValue::FromText("q\"")), R"("q\"")");
    }

    TEST(ArtGenInputValueTest, AnIntegerEqualsTheSameNumberAndNothingElseIsEqualAcrossKinds) {
        EXPECT_EQ(ArtGenInputValue::FromInteger(1), ArtGenInputValue::FromNumber(1.0));
        EXPECT_NE(ArtGenInputValue::FromInteger(1), ArtGenInputValue::FromNumber(1.5));
        EXPECT_NE(ArtGenInputValue::FromInteger(1), ArtGenInputValue::FromBool(true));
        EXPECT_NE(ArtGenInputValue::FromInteger(0), ArtGenInputValue{});
        EXPECT_NE(ArtGenInputValue::FromText("1"), ArtGenInputValue::FromInteger(1));
        EXPECT_EQ(ReadOf(R"({"a":1,"b":[true]})"), ReadOf(R"({"b":[true],"a":1.0})")) << "two other values are equal as JSON";
        EXPECT_NE(ReadOf("[1,2]"), ReadOf("[2,1]"));
    }

    TEST(ArtGenInputValueTest, TheFormShowsAStringBareAndANumberShortAndNothingForTheRest) {
        EXPECT_EQ(Shown(ArtGenInputValue::FromText("euler")), "euler");
        EXPECT_EQ(Shown(ArtGenInputValue::FromInteger(-12)), "-12");
        EXPECT_EQ(Shown(ArtGenInputValue::FromNumber(6.5)), "6.5");
        EXPECT_EQ(Shown(ArtGenInputValue::FromBool(false)), "false");
        EXPECT_EQ(Shown(ArtGenInputValue{}), "");
        EXPECT_EQ(Shown(ReadOf("[1]")), "");

        char small[4];
        ArtGenInputValue::FromText("abcdefgh").ToText(small, sizeof(small));
        EXPECT_STREQ(small, "abc") << "cut to the buffer, with a NUL";
    }

}  // namespace BECore::Tests
