#include "ReflectionFieldAttributesTest.h"

#include <BECore/Reflection/IPropertyVisitor.h>
#include <BECore/Reflection/PropertyVisitorDispatch.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/NullPropertyVisitor.h>
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

        /// Overrides no field hook: the defaults of BeginField, EndField and ShowsHidden must be enough to edit a field
        class IncrementingVisitor final : public NullPropertyVisitor {
        public:
            using NullPropertyVisitor::Visit;
            bool Visit(StringView, int32_t& value) override {
                ++value;
                return true;
            }
        };

        CORE_ENUM(EventKind, uint8_t, Begin, Visit, BeginCompound, EndCompound, End)

        struct Event {
            EventKind kind;
            String name;
            FieldAttributes attributes;
        };

        class RecordingVisitor final : public NullPropertyVisitor {
        public:
            using NullPropertyVisitor::Visit;

            bool showsHidden = false;
            Vector<Event> events;

            void BeginField(const FieldAttributes& attributes) override {
                events.push_back({EventKind::Begin, String{}, attributes});
            }
            void EndField() override {
                events.push_back({EventKind::End, String{}, FieldAttributes{}});
            }
            bool ShowsHidden() const override {
                return showsHidden;
            }
            bool Visit(StringView name, int32_t&) override {
                Record(EventKind::Visit, name);
                return false;
            }
            bool Visit(StringView name, float&) override {
                Record(EventKind::Visit, name);
                return false;
            }
            bool Visit(StringView name, bool&) override {
                Record(EventKind::Visit, name);
                return false;
            }
            bool Visit(StringView name, String&) override {
                Record(EventKind::Visit, name);
                return false;
            }
            bool BeginCompound(StringView name) override {
                Record(EventKind::BeginCompound, name);
                return true;
            }
            void EndCompound() override {
                Record(EventKind::EndCompound, StringView{});
            }

            /// The visits of the log as `name` and `{`, `}` around a field: `{ opacity } { count } ...`
            String Trace() const {
                String text;
                for (const Event& event : events) {
                    switch (event.kind) {
                        case EventKind::Begin:
                            text += "{ ";
                            break;
                        case EventKind::End:
                            text += "} ";
                            break;
                        case EventKind::BeginCompound:
                            text += "<";
                            text.append(event.name.data(), event.name.size());
                            text += "> ";
                            break;
                        case EventKind::EndCompound:
                            text += "</> ";
                            break;
                        case EventKind::Visit:
                            text.append(event.name.data(), event.name.size());
                            text += " ";
                            break;
                    }
                }
                return text;
            }

        private:
            void Record(EventKind kind, StringView name) {
                events.push_back({kind, String(name.data(), name.size()), FieldAttributes{}});
            }
        };

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

    TEST(ReflectionFieldAttributesTest, VisitorHearsBeginVisitEndPerFieldInOrder) {
        Annotated state;
        RecordingVisitor visitor;
        EXPECT_FALSE(state.AcceptPropertyVisitor(visitor));
        // _flags is hidden: no bracket, no visit
        EXPECT_EQ(visitor.Trace(), "{ opacity } { count } { quoted } { plain } ");

        ASSERT_EQ(visitor.events.size(), 12u);
        EXPECT_EQ(visitor.events[0].attributes, std::get<0>(AnnotatedFields).attributes);
        EXPECT_EQ(visitor.events[3].attributes, std::get<1>(AnnotatedFields).attributes);
        EXPECT_EQ(visitor.events[6].attributes, std::get<3>(AnnotatedFields).attributes);
        EXPECT_EQ(visitor.events[9].attributes, FieldAttributes{});
    }

    TEST(ReflectionFieldAttributesTest, HiddenFieldIsVisitedWhenTheVisitorAsks) {
        Annotated state;
        RecordingVisitor visitor;
        visitor.showsHidden = true;
        state.AcceptPropertyVisitor(visitor);
        EXPECT_EQ(visitor.Trace(), "{ opacity } { count } { flags } { quoted } { plain } ");
        EXPECT_TRUE(visitor.events[6].attributes.hidden);
    }

    TEST(ReflectionFieldAttributesTest, InheritedFieldsAreBracketedToo) {
        Derived state;
        RecordingVisitor visitor;
        state.AcceptPropertyVisitor(visitor);
        // baseValue is hidden in the base class
        EXPECT_EQ(visitor.Trace(), "{ ownValue } ");
        visitor.events.clear();
        visitor.showsHidden = true;
        state.AcceptPropertyVisitor(visitor);
        EXPECT_EQ(visitor.Trace(), "{ baseValue } { ownValue } ");
    }

    TEST(ReflectionFieldAttributesTest, NestedCompoundGivesMatchedBrackets) {
        Outer state;
        RecordingVisitor visitor;
        state.AcceptPropertyVisitor(visitor);
        EXPECT_EQ(visitor.Trace(), "{ <inner> { innerValue } </> } { after } ");
        // the attributes of the outer field come before its compound, the inner field's inside
        ASSERT_GE(visitor.events.size(), 3u);
        EXPECT_TRUE(visitor.events[0].attributes.readOnly);
        EXPECT_EQ(visitor.events[2].attributes.tooltip, "inner field");
    }

    TEST(ReflectionFieldAttributesTest, VisitorWithoutFieldHooksStillEdits) {
        Annotated state;
        IncrementingVisitor visitor;
        EXPECT_TRUE(state.AcceptPropertyVisitor(visitor));
        EXPECT_EQ(state.count, 4);
        EXPECT_EQ(state.plain, 8);
    }

    TEST(ReflectionFieldAttributesTest, ChangedIsTrueOnlyWhenAVisitChangedAValue) {
        Annotated state;
        NullPropertyVisitor nothing;
        EXPECT_FALSE(state.AcceptPropertyVisitor(nothing));

        Outer outer;
        IncrementingVisitor increment;
        EXPECT_TRUE(outer.AcceptPropertyVisitor(increment));
        EXPECT_EQ(outer.inner.innerValue, 2);
        EXPECT_EQ(outer.after, 3);

        // a changed value of a hidden field does not count: the field is not visited
        Base base;
        EXPECT_FALSE(base.AcceptPropertyVisitor(increment));
        EXPECT_EQ(base.baseValue, 1);
    }

}  // namespace BECore::Tests
