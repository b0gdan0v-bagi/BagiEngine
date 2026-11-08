#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        /// One element `<root a="TEXT"/>` read through ReadAttribute, strict or lenient.
        class AttributeCase {
        public:
            AttributeCase(eastl::string_view text, bool strict) {
                _deserializer.SetStrictValues(strict);
                const eastl::string xml = "<root a=\"" + eastl::string(text.data(), text.size()) + "\"/>";
                EXPECT_TRUE(_deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            }

            template <typename T>
            bool Read(T& value) {
                return _deserializer.ReadAttribute("a", value);
            }

            XmlDeserializer& Get() {
                return _deserializer;
            }

        private:
            XmlDeserializer _deserializer;
        };

        template <typename T>
        bool StrictReads(eastl::string_view text, T expected) {
            AttributeCase c(text, true);
            T value{};
            return c.Read(value) && value == expected && !c.Get().HasErrors();
        }

        /// Rejected: false, the target untouched, one error under the name of the attribute.
        template <typename T>
        bool StrictRejects(eastl::string_view text) {
            AttributeCase c(text, true);
            T value = static_cast<T>(42);
            const bool read = c.Read(value);
            const auto& errors = c.Get().GetErrors();
            return !read && value == static_cast<T>(42) && errors.size() == 1 && errors[0].path.ToStringView() == "a";
        }

    }  // namespace

    TEST(XmlStrictValuesTest, DefaultsToOff) {
        XmlDeserializer deserializer;
        EXPECT_FALSE(deserializer.IsStrictValues());
        deserializer.SetStrictValues(true);
        EXPECT_TRUE(deserializer.IsStrictValues());
        deserializer.SetStrictValues(false);
        EXPECT_FALSE(deserializer.IsStrictValues());
    }

    TEST(XmlStrictValuesTest, LenientByDefaultReadsGarbageAsZero) {
        AttributeCase number("abc", false);
        uint16_t port = 7;
        EXPECT_TRUE(number.Read(port));
        EXPECT_EQ(port, 0);

        AttributeCase yes("yes", false);
        bool flag = true;
        EXPECT_TRUE(yes.Read(flag));
        EXPECT_FALSE(flag);

        AttributeCase tooBig("70000", false);
        uint16_t wrapped = 7;
        EXPECT_TRUE(tooBig.Read(wrapped));
        EXPECT_EQ(wrapped, static_cast<uint16_t>(70000));

        EXPECT_FALSE(number.Get().HasErrors());
        EXPECT_FALSE(yes.Get().HasErrors());
        EXPECT_FALSE(tooBig.Get().HasErrors());
    }

    TEST(XmlStrictValuesTest, BoolAcceptsTheFourSpellings) {
        EXPECT_TRUE(StrictReads<bool>("true", true));
        EXPECT_TRUE(StrictReads<bool>("1", true));
        EXPECT_TRUE(StrictReads<bool>("false", false));
        EXPECT_TRUE(StrictReads<bool>("0", false));
    }

    TEST(XmlStrictValuesTest, BoolRejectsEverythingElse) {
        EXPECT_TRUE(StrictRejects<bool>("yes"));
        EXPECT_TRUE(StrictRejects<bool>("True"));
        EXPECT_TRUE(StrictRejects<bool>("2"));
        EXPECT_TRUE(StrictRejects<bool>(""));
        EXPECT_TRUE(StrictRejects<bool>("truee"));
    }

    TEST(XmlStrictValuesTest, ErrorTextNamesTheKindAndTheValue) {
        AttributeCase c("yes", true);
        bool flag = false;
        EXPECT_FALSE(c.Read(flag));
        ASSERT_EQ(c.Get().GetErrors().size(), 1u);
        EXPECT_EQ(c.Get().GetErrors()[0].errorMessage.ToStringView(), "Invalid boolean value 'yes'");

        AttributeCase number("12abc", true);
        int32_t value = 0;
        EXPECT_FALSE(number.Read(value));
        ASSERT_EQ(number.Get().GetErrors().size(), 1u);
        EXPECT_EQ(number.Get().GetErrors()[0].errorMessage.ToStringView(), "Invalid integer value '12abc'");

        AttributeCase range("65536", true);
        uint16_t port = 0;
        EXPECT_FALSE(range.Read(port));
        ASSERT_EQ(range.Get().GetErrors().size(), 1u);
        EXPECT_EQ(range.Get().GetErrors()[0].errorMessage.ToStringView(), "Invalid integer value '65536' (out of range)");

        AttributeCase real("x", true);
        double d = 0;
        EXPECT_FALSE(real.Read(d));
        ASSERT_EQ(real.Get().GetErrors().size(), 1u);
        EXPECT_EQ(real.Get().GetErrors()[0].errorMessage.ToStringView(), "Invalid number value 'x'");
    }

    TEST(XmlStrictValuesTest, Uint16TakesTheWholeRangeAndNothingElse) {
        EXPECT_TRUE(StrictReads<uint16_t>("0", 0));
        EXPECT_TRUE(StrictReads<uint16_t>("65535", 65535));
        EXPECT_TRUE(StrictRejects<uint16_t>("65536"));
        EXPECT_TRUE(StrictRejects<uint16_t>("-1"));
        EXPECT_TRUE(StrictRejects<uint16_t>("1.5"));
        EXPECT_TRUE(StrictRejects<uint16_t>("12abc"));
        EXPECT_TRUE(StrictRejects<uint16_t>(""));
        EXPECT_TRUE(StrictRejects<uint16_t>("abc"));
        EXPECT_TRUE(StrictRejects<uint16_t>("+-1"));
    }

    TEST(XmlStrictValuesTest, SpacesAroundTheNumberAndALeadingPlusAreAccepted) {
        EXPECT_TRUE(StrictReads<uint16_t>(" 8080 ", 8080));
        EXPECT_TRUE(StrictReads<int32_t>("+5", 5));
        EXPECT_TRUE(StrictReads<bool>(" true ", true));
        EXPECT_TRUE(StrictRejects<int32_t>("1 2"));
        EXPECT_TRUE(StrictRejects<int32_t>("0x10"));
        EXPECT_TRUE(StrictRejects<int32_t>("   "));
    }

    TEST(XmlStrictValuesTest, IntegerLimits) {
        EXPECT_TRUE(StrictReads<int8_t>("127", 127));
        EXPECT_TRUE(StrictReads<int8_t>("-128", -128));
        EXPECT_TRUE(StrictRejects<int8_t>("128"));
        EXPECT_TRUE(StrictRejects<int8_t>("-129"));

        EXPECT_TRUE(StrictReads<uint8_t>("255", 255));
        EXPECT_TRUE(StrictRejects<uint8_t>("256"));

        EXPECT_TRUE(StrictReads<int16_t>("-32768", -32768));
        EXPECT_TRUE(StrictRejects<int16_t>("32768"));

        EXPECT_TRUE(StrictReads<uint32_t>("4294967295", 4294967295u));
        EXPECT_TRUE(StrictRejects<uint32_t>("4294967296"));
        EXPECT_TRUE(StrictRejects<uint32_t>("-1"));

        EXPECT_TRUE(StrictReads<int32_t>("-2147483648", INT32_MIN));
        EXPECT_TRUE(StrictRejects<int32_t>("2147483648"));

        EXPECT_TRUE(StrictReads<int64_t>("9223372036854775807", INT64_MAX));
        EXPECT_TRUE(StrictReads<int64_t>("-9223372036854775808", INT64_MIN));
        EXPECT_TRUE(StrictRejects<int64_t>("9223372036854775808"));

        EXPECT_TRUE(StrictReads<uint64_t>("18446744073709551615", UINT64_MAX));
        EXPECT_TRUE(StrictRejects<uint64_t>("18446744073709551616"));
        EXPECT_TRUE(StrictRejects<uint64_t>("-1"));
    }

    TEST(XmlStrictValuesTest, FloatAndDoubleTakeNumbersOnly) {
        EXPECT_TRUE(StrictReads<float>("1.5", 1.5f));
        EXPECT_TRUE(StrictReads<float>("-2", -2.0f));
        EXPECT_TRUE(StrictReads<float>("1e3", 1000.0f));
        EXPECT_TRUE(StrictRejects<float>("1.5f"));
        EXPECT_TRUE(StrictRejects<float>("x"));
        EXPECT_TRUE(StrictRejects<float>(""));
        EXPECT_TRUE(StrictRejects<float>("1e999"));
        EXPECT_TRUE(StrictRejects<float>("nan"));
        EXPECT_TRUE(StrictRejects<float>("inf"));

        EXPECT_TRUE(StrictReads<double>("1.5", 1.5));
        EXPECT_TRUE(StrictReads<double>("-2", -2.0));
        EXPECT_TRUE(StrictReads<double>("1e3", 1000.0));
        EXPECT_TRUE(StrictRejects<double>("1.5f"));
        EXPECT_TRUE(StrictRejects<double>("x"));
        EXPECT_TRUE(StrictRejects<double>("1e999"));
    }

    TEST(XmlStrictValuesTest, ATargetKeepsItsValueOnAnError) {
        AttributeCase c("abc", true);
        int32_t value = 123;
        EXPECT_FALSE(c.Read(value));
        EXPECT_EQ(value, 123);
        EXPECT_TRUE(c.Get().HasErrors());
    }

    TEST(XmlStrictValuesTest, AMissingAttributeIsNoErrorInEitherMode) {
        for (const bool strict : {false, true}) {
            XmlDeserializer deserializer;
            deserializer.SetStrictValues(strict);
            ASSERT_TRUE(deserializer.LoadFromString("<root/>"));
            int32_t number = 5;
            bool flag = true;
            EXPECT_FALSE(deserializer.ReadAttribute("a", number));
            EXPECT_FALSE(deserializer.ReadAttribute("a", flag));
            EXPECT_EQ(number, 5);
            EXPECT_TRUE(flag);
            EXPECT_FALSE(deserializer.HasErrors());
        }
    }

    TEST(XmlStrictValuesTest, ReadOfAChildElementIsStrictToo) {
        XmlDeserializer deserializer;
        deserializer.SetStrictValues(true);
        ASSERT_TRUE(deserializer.LoadFromString("<root><port>70000</port><ok>8080</ok><on>yes</on><off>0</off></root>"));

        uint16_t port = 1;
        EXPECT_FALSE(deserializer.Read("port", port));
        EXPECT_EQ(port, 1);
        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        EXPECT_EQ(deserializer.GetErrors()[0].path.ToStringView(), "port");

        uint16_t ok = 1;
        EXPECT_TRUE(deserializer.Read("ok", ok));
        EXPECT_EQ(ok, 8080);

        bool on = false;
        EXPECT_FALSE(deserializer.Read("on", on));
        EXPECT_FALSE(on);
        EXPECT_EQ(deserializer.GetErrors().size(), 2u);

        bool off = true;
        EXPECT_TRUE(deserializer.Read("off", off));
        EXPECT_FALSE(off);

        // A missing element is reported in both modes, as it always was
        int32_t missing = 0;
        EXPECT_FALSE(deserializer.Read("nothing", missing));
        EXPECT_EQ(deserializer.GetErrors().size(), 3u);
    }

    TEST(XmlStrictValuesTest, LenientReadOfAChildElementIsUnchanged) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><n>abc</n><b>yes</b></root>"));
        int32_t n = 9;
        bool b = true;
        EXPECT_TRUE(deserializer.Read("n", n));
        EXPECT_TRUE(deserializer.Read("b", b));
        EXPECT_EQ(n, 0);
        EXPECT_FALSE(b);
        EXPECT_FALSE(deserializer.HasErrors());
    }

    TEST(XmlStrictValuesTest, TheFlagHoldsInsideNestedObjects) {
        XmlDeserializer deserializer;
        deserializer.SetStrictValues(true);
        ASSERT_TRUE(deserializer.LoadFromString("<root><inner n=\"bad\"/></root>"));

        ASSERT_TRUE(deserializer.BeginObject("inner"));
        int32_t n = 4;
        EXPECT_FALSE(deserializer.ReadAttribute("n", n));
        EXPECT_EQ(n, 4);
        deserializer.EndObject();

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        EXPECT_EQ(deserializer.GetErrors()[0].path.ToStringView(), "inner.n");
        EXPECT_TRUE(deserializer.IsStrictValues());
    }

    TEST(XmlStrictValuesTest, TheFlagSurvivesAMove) {
        XmlDeserializer first;
        first.SetStrictValues(true);
        XmlDeserializer second(std::move(first));
        EXPECT_TRUE(second.IsStrictValues());
    }

    TEST(XmlStrictValuesTest, StringsAreNotChecked) {
        XmlDeserializer deserializer;
        deserializer.SetStrictValues(true);
        ASSERT_TRUE(deserializer.LoadFromString("<root s=\"12abc\"/>"));
        eastl::string text;
        EXPECT_TRUE(deserializer.ReadAttribute("s", text));
        EXPECT_EQ(text, "12abc");
        EXPECT_FALSE(deserializer.HasErrors());
    }

}  // namespace BECore::Tests
