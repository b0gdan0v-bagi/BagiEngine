#include "ReflectionFieldAttributesTest.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Generated/ReflectionFieldAttributesTest.gen.hpp>

namespace BECore::Tests {

    using namespace FieldAttributesTestData;

    namespace {

        constexpr auto& AnnotatedFields = ReflectionTraits<Annotated>::fields;

        // What the generator wrote, at compile time
        static_assert(std::get<0>(AnnotatedFields).name == "_opacity");
        static_assert(std::get<0>(AnnotatedFields).attributes.hasRange);
        static_assert(std::get<0>(AnnotatedFields).attributes.rangeMin == 0.0);
        static_assert(std::get<0>(AnnotatedFields).attributes.rangeMax == 1.0);
        static_assert(std::get<0>(AnnotatedFields).attributes.step == 0.05);
        static_assert(std::get<0>(AnnotatedFields).attributes.tooltip == "Share of the screen");
        static_assert(std::get<4>(AnnotatedFields).attributes == FieldAttributes{});

        template <typename State>
        String SaveXml(const State& state) {
            XmlSerializer serializer;
            if (serializer.BeginObject("State")) {
                state.Serialize(serializer);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

    }  // namespace

    TEST(ReflectionFieldAttributesTest, RangeAndStepAreExact) {
        const FieldAttributes& opacity = std::get<0>(AnnotatedFields).attributes;
        EXPECT_TRUE(opacity.hasRange);
        EXPECT_EQ(opacity.rangeMin, 0.0);
        EXPECT_EQ(opacity.rangeMax, 1.0);
        EXPECT_EQ(opacity.step, 0.05);
        EXPECT_EQ(opacity.tooltip, "Share of the screen");
        EXPECT_FALSE(opacity.readOnly);
        EXPECT_FALSE(opacity.hidden);
        EXPECT_FALSE(opacity.required);

        const FieldAttributes& count = std::get<1>(AnnotatedFields).attributes;
        EXPECT_TRUE(count.hasRange);
        EXPECT_EQ(count.rangeMin, -10.0);
        EXPECT_EQ(count.rangeMax, 1000.0);
        EXPECT_EQ(count.step, 0.5);
        EXPECT_TRUE(count.tooltip.empty());
    }

    TEST(ReflectionFieldAttributesTest, FlagsAreSet) {
        const FieldAttributes& flags = std::get<2>(AnnotatedFields).attributes;
        EXPECT_TRUE(flags.readOnly);
        EXPECT_TRUE(flags.hidden);
        EXPECT_TRUE(flags.required);
        EXPECT_FALSE(flags.hasRange);
        EXPECT_EQ(flags.step, 0.0);
    }

    TEST(ReflectionFieldAttributesTest, TooltipKeepsQuoteBackslashAndUtf8) {
        const FieldAttributes& quoted = std::get<3>(AnnotatedFields).attributes;
        EXPECT_EQ(quoted.tooltip, "say \"hi\" \\ caf\xC3\xA9, (really)");
    }

    TEST(ReflectionFieldAttributesTest, FieldWithoutAttributesHasTheDefaults) {
        EXPECT_EQ(std::get<4>(AnnotatedFields).attributes, FieldAttributes{});
        EXPECT_EQ(std::get<0>(ReflectionTraits<Unannotated>::fields).attributes, FieldAttributes{});
        const FieldAttributes defaults;
        EXPECT_FALSE(defaults.hasRange);
        EXPECT_EQ(defaults.step, 0.0);
        EXPECT_TRUE(defaults.tooltip.empty());
    }

    TEST(ReflectionFieldAttributesTest, FieldsOfAnInheritedClassKeepTheirOwnAttributes) {
        EXPECT_EQ(FieldCount<Derived>(), 1u);
        const FieldAttributes& own = std::get<0>(ReflectionTraits<Derived>::fields).attributes;
        EXPECT_TRUE(own.hasRange);
        EXPECT_EQ(own.rangeMax, 5.0);
        EXPECT_FALSE(own.hidden);
        EXPECT_TRUE(std::get<0>(ReflectionTraits<Base>::fields).attributes.hidden);
    }

    TEST(ReflectionFieldAttributesTest, SerializedFormIsTheSameWithoutTheAttributes) {
        Annotated annotated;
        Unannotated plain;
        annotated._quoted = "text";
        plain._quoted = "text";
        EXPECT_EQ(SaveXml(annotated), SaveXml(plain));
        EXPECT_NE(SaveXml(annotated).find("opacity=\"0.5\""), String::npos);
    }

    TEST(ReflectionFieldAttributesTest, RoundTripIsUnchanged) {
        Annotated source;
        source._opacity = 0.25f;
        source.count = -4;
        source._flags = false;
        source._quoted = "caf\xC3\xA9";
        source.plain = 99;
        const String xml = SaveXml(source);

        Annotated loaded;
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(xml)) << xml.c_str();
        ASSERT_TRUE(deserializer.BeginObject("State"));
        loaded.Deserialize(deserializer);
        deserializer.EndObject();
        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(loaded._opacity, 0.25f);
        EXPECT_EQ(loaded.count, -4);
        EXPECT_FALSE(loaded._flags);
        EXPECT_EQ(loaded._quoted, "caf\xC3\xA9");
        EXPECT_EQ(loaded.plain, 99);
    }

}  // namespace BECore::Tests
