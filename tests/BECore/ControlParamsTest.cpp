// The checked readers of a call's params over a JsonView (Control/ControlParams.h) and the overloads of ReadControlFrames / ReadControlFrameSeconds
// that use them: what the `GetJson*` readers said for a DOM, a view says for a tape. A number past the double range is the one difference:
// the DOM refused the whole text at parse, the tape holds it and the reader refuses it.

#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonTape.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// A document and its tape; the tape borrows the text, so both live as long as the fixture
        class Params {
        public:
            explicit Params(StringView text) : _text(text) {
                EXPECT_TRUE(_tape.Parse(_text)) << _text.c_str();
            }

            Params(const Params&) = delete;
            Params& operator=(const Params&) = delete;

            [[nodiscard]] JsonView View() const {
                return JsonView(_tape, 0);
            }

        private:
            String _text;
            JsonTape _tape;
        };

    }  // namespace

    TEST(ControlParamsTest, FindParamTellsAnAbsentKeyFromAMemberAndAnyKindOfParamsFromAnObject) {
        const Params object(R"({"a":1,"b":null})");
        EXPECT_TRUE(FindParam(object.View(), "a"));
        EXPECT_TRUE(FindParam(object.View(), "b"));
        EXPECT_FALSE(FindParam(object.View(), "c"));

        for (const StringView text : {StringView("[1]"), StringView("5"), StringView("\"a\""), StringView("null")}) {
            const Params other(text);
            EXPECT_FALSE(FindParam(other.View(), "a")) << text.data();
            EXPECT_FALSE(GetParamInt(other.View(), "a"));
            EXPECT_FALSE(GetParamString(other.View(), "a"));
        }
        EXPECT_FALSE(FindParam(JsonView(), "a"));
        EXPECT_FALSE(GetParamBool(JsonView(), "a"));
    }

    TEST(ControlParamsTest, GetParamStringDecodesEscapesAndKeepsAnEmbeddedNul) {
        const Params params(R"({"plain":"abc","escaped":"a\n\"b\"é","nul":"x\u0000y","empty":"","number":1,"list":["a"]})");
        EXPECT_EQ(GetParamString(params.View(), "plain"), String("abc"));
        EXPECT_EQ(GetParamString(params.View(), "escaped"), String("a\n\"b\"\xC3\xA9"));
        const Optional<String> withNul = GetParamString(params.View(), "nul");
        ASSERT_TRUE(withNul);
        EXPECT_EQ(StringView(*withNul), StringView("x\0y", 3));
        EXPECT_EQ(GetParamString(params.View(), "empty"), String());

        EXPECT_FALSE(GetParamString(params.View(), "number"));
        EXPECT_FALSE(GetParamString(params.View(), "list"));
        EXPECT_FALSE(GetParamString(params.View(), "missing"));
    }

    TEST(ControlParamsTest, GetParamIntTakesOnlyAnIntegerThatFitsInt64) {
        const Params params(R"({"ok":42,"neg":-7,"zero":-0,"max":9223372036854775807,"min":-9223372036854775808,)"
                            R"("past":9223372036854775808,"huge":123456789012345678901234567890,"fraction":1.5,"exp":1e2,"text":"3","flag":true,"none":null})");
        EXPECT_EQ(GetParamInt(params.View(), "ok"), 42);
        EXPECT_EQ(GetParamInt(params.View(), "neg"), -7);
        EXPECT_EQ(GetParamInt(params.View(), "zero"), 0);
        EXPECT_EQ(GetParamInt(params.View(), "max"), INT64_MAX);
        EXPECT_EQ(GetParamInt(params.View(), "min"), INT64_MIN);

        for (const StringView key :
             {StringView("past"), StringView("huge"), StringView("fraction"), StringView("exp"), StringView("text"), StringView("flag"), StringView("none"), StringView("missing")}) {
            EXPECT_FALSE(GetParamInt(params.View(), key)) << key.data();
        }
    }

    TEST(ControlParamsTest, GetParamNumberTakesIntegersAndFloatsThatAreFinite) {
        const Params params(R"({"int":3,"float":2.5,"exp":1e2,"zero":-0,"big":123456789012345678901234567890,"inf":1e999,"ninf":-1e999,"text":"1","flag":false})");
        EXPECT_EQ(GetParamNumber(params.View(), "int"), 3.0);
        EXPECT_EQ(GetParamNumber(params.View(), "float"), 2.5);
        EXPECT_EQ(GetParamNumber(params.View(), "exp"), 100.0);
        const Optional<double> zero = GetParamNumber(params.View(), "zero");
        ASSERT_TRUE(zero);
        EXPECT_EQ(*zero, 0.0);
        const Optional<double> big = GetParamNumber(params.View(), "big");
        ASSERT_TRUE(big);
        EXPECT_NEAR(*big, 1.2345678901234568e29, 1e15);

        for (const StringView key : {StringView("inf"), StringView("ninf"), StringView("text"), StringView("flag"), StringView("missing")}) {
            EXPECT_FALSE(GetParamNumber(params.View(), key)) << key.data();
        }
    }

    TEST(ControlParamsTest, GetParamBoolTakesOnlyABoolean) {
        const Params params(R"({"yes":true,"no":false,"one":1,"text":"true","none":null})");
        EXPECT_EQ(GetParamBool(params.View(), "yes"), true);
        EXPECT_EQ(GetParamBool(params.View(), "no"), false);
        for (const StringView key : {StringView("one"), StringView("text"), StringView("none"), StringView("missing")}) {
            EXPECT_FALSE(GetParamBool(params.View(), key)) << key.data();
        }
    }

    TEST(ControlParamsTest, AKeyWrittenWithAnEscapeIsFound) {
        const Params params(R"({"frames":4})");
        EXPECT_EQ(GetParamInt(params.View(), "frames"), 4);
    }

    TEST(ControlParamsTest, ReadControlFramesOverAViewGivesTheFallbackForAnAbsentKeyAndRefusesWhatIsNotInRange) {
        const Params absent(R"({})");
        const auto fallback = ReadControlFrames(absent.View(), "frames", 7);
        ASSERT_TRUE(fallback);
        EXPECT_EQ(*fallback, 7u);

        const Params set(R"({"frames":12})");
        EXPECT_EQ(*ReadControlFrames(set.View(), "frames", 1), 12u);
        const Params limit(R"({"frames":36000})");
        EXPECT_EQ(*ReadControlFrames(limit.View(), "frames", 1), kMaxStepFrames);

        for (const StringView text : {StringView(R"({"frames":0})"), StringView(R"({"frames":36001})"), StringView(R"({"frames":-1})"), StringView(R"({"frames":1.5})"),
                                      StringView(R"({"frames":"3"})"), StringView(R"({"frames":null})"), StringView(R"({"frames":99999999999999999999})")}) {
            const Params bad(text);
            const auto frames = ReadControlFrames(bad.View(), "frames", 1);
            ASSERT_FALSE(frames) << text.data();
            EXPECT_EQ(frames.error().code, ControlErrors::kInvalidParams) << text.data();
        }
    }

    TEST(ControlParamsTest, ReadControlFrameSecondsOverAViewGivesTheDefaultForAnAbsentKeyAndRefusesWhatIsNotInRange) {
        const Params absent(R"({})");
        EXPECT_TRUE(ReadControlFrameSeconds(absent.View()));

        const Params set(R"({"dt":0.125})");
        EXPECT_EQ(*ReadControlFrameSeconds(set.View()), 0.125);
        const Params zero(R"({"dt":0})");
        EXPECT_EQ(*ReadControlFrameSeconds(zero.View()), 0.0);

        for (const StringView text : {StringView(R"({"dt":-1})"), StringView(R"({"dt":1e999})"), StringView(R"({"dt":"x"})"), StringView(R"({"dt":true})"), StringView(R"({"dt":1000000})")}) {
            const Params bad(text);
            const auto seconds = ReadControlFrameSeconds(bad.View());
            ASSERT_FALSE(seconds) << text.data();
            EXPECT_EQ(seconds.error().code, ControlErrors::kInvalidParams) << text.data();
        }
    }

}  // namespace BECore::Tests
