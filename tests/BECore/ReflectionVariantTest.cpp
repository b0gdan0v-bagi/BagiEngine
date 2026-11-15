#include "ReflectionVariantTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <Common/Printers.h>
#include <Generated/ReflectionVariantTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using ReflectionVariantTestData::Point;

    namespace {

        /// One alternative of every kind: empty, scalars, a string, an object
        using Choice = Variant<std::monostate, int32_t, float, String, Point>;
        using ChoiceList = Vector<Choice>;
        using ChoiceTable = HashMap<String, Choice>;
        using ChoiceOrder = Map<int32_t, Choice>;
        using Twins = Variant<int32_t, int32_t>;

        constexpr size_t kChoiceCount = std::variant_size_v<Choice>;

        Point MakePoint(int32_t x, int32_t y) {
            Point point;
            point.x = x;
            point.y = y;
            return point;
        }

        /// A value of every alternative, in the order of the alternatives
        Vector<Choice> EveryAlternative() {
            Vector<Choice> out;
            out.push_back(Choice{});
            out.push_back(Choice{std::in_place_index<1>, -42});
            out.push_back(Choice{std::in_place_index<2>, 2.5f});
            out.push_back(Choice{std::in_place_index<3>, String("caf\xC3\xA9 <&>")});
            out.push_back(Choice{std::in_place_index<4>, MakePoint(3, -4)});
            return out;
        }

        /// A value of another alternative than @p value, so that a load that does nothing shows
        Choice Stale(const Choice& value) {
            return value.index() == 1 ? Choice{std::in_place_index<3>, String("stale")} : Choice{std::in_place_index<1>, 99};
        }

        constexpr StringView kKey = "choice";

        template <typename T>
        String SaveXml(const T& value) {
            XmlSerializer serializer;
            if (serializer.BeginObject("State")) {
                Ser::Save(serializer, value, kKey);
                serializer.EndObject();
            }
            return serializer.SaveToString();
        }

        template <typename T>
        Vector<uint8_t> SaveBinary(const T& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject("State")) {
                Ser::Save(serializer, value, kKey);
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        /// Loads the field `choice` of `<root><State>` (the shape XmlSerializer writes) from @p xml under @p mode
        template <typename T>
        struct XmlLoad {
            XmlDeserializer reader;
            bool loaded = false;

            XmlLoad(StringView xml, T& value, DiagnosticsMode mode = DiagnosticsMode::Off) {
                reader.SetDiagnostics(mode);
                EXPECT_TRUE(reader.LoadFromString(xml)) << String(xml).c_str();
                EXPECT_TRUE(reader.BeginObject("State"));
                loaded = Ser::Load(reader, value, kKey);
                reader.EndObject();
            }

            /// Path and message of every error, then of every warning
            Vector<String> Reports() const {
                Vector<String> out;
                for (const Vector<DeserializeError>* list : {&reader.GetErrors(), &reader.GetWarnings()}) {
                    for (const DeserializeError& report : *list) {
                        out.push_back(Format("{}: {}", report.path.ToStringView(), report.errorMessage.ToStringView()));
                    }
                }
                return out;
            }
        };

        template <typename T>
        bool LoadBinary(const Vector<uint8_t>& data, T& value) {
            BinaryDeserializer reader;
            EXPECT_TRUE(reader.LoadFromBuffer(data));
            EXPECT_TRUE(reader.BeginObject("State"));
            const bool loaded = Ser::Load(reader, value, kKey);
            reader.EndObject();
            EXPECT_FALSE(reader.HasErrors());
            return loaded;
        }

        bool HasText(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        /// Saves @p value, loads it back under @p mode and expects no error and no finding
        template <typename T>
        void ExpectClean(const T& value, DiagnosticsMode mode) {
            const String xml = SaveXml(value);
            T loaded{};
            XmlLoad load(xml, loaded, mode);
            EXPECT_TRUE(load.Reports().empty()) << xml.c_str();
            EXPECT_EQ(loaded, value) << xml.c_str();
        }

    }  // namespace

    static_assert(Ser::HasDataAccessor<Choice>);
    static_assert(Ser::HasDataAccessor<std::monostate>);
    static_assert(Ser::HasDataAccessor<ChoiceList>);
    static_assert(Ser::HasDataAccessor<ChoiceTable>);
    static_assert(Ser::HasDataAccessor<ChoiceOrder>);

    TEST(ReflectionVariantTest, CompileTimeChecks) {
        SUCCEED() << "The static_asserts at the top of this file are the check";
    }

    // =========================================================================
    // A field
    // =========================================================================

    TEST(ReflectionVariantTest, EveryAlternativeRoundTripsInXml) {
        for (const Choice& original : EveryAlternative()) {
            const String xml = SaveXml(original);
            EXPECT_TRUE(HasText(xml, Format("<choice index=\"{}\"", original.index()))) << xml.c_str();

            Choice loaded = Stale(original);
            XmlLoad load(xml, loaded);
            EXPECT_TRUE(load.loaded) << xml.c_str();
            EXPECT_TRUE(load.Reports().empty()) << xml.c_str();
            EXPECT_EQ(loaded, original) << xml.c_str();
        }
    }

    TEST(ReflectionVariantTest, AScalarIsTheAttributeValueAndAnObjectTheChildValue) {
        const String scalar = SaveXml(Choice{std::in_place_index<1>, -42});
        EXPECT_TRUE(HasText(scalar, "<choice index=\"1\" value=\"-42\" />")) << scalar.c_str();

        const String text = SaveXml(Choice{std::in_place_index<3>, String("word")});
        EXPECT_TRUE(HasText(text, "<choice index=\"3\" value=\"word\" />")) << text.c_str();

        const String object = SaveXml(Choice{std::in_place_index<4>, MakePoint(3, -4)});
        EXPECT_TRUE(HasText(object, "<choice index=\"4\">")) << object.c_str();
        EXPECT_TRUE(HasText(object, "<value x=\"3\" y=\"-4\" />")) << object.c_str();
    }

    TEST(ReflectionVariantTest, EveryAlternativeRoundTripsInBinary) {
        for (const Choice& original : EveryAlternative()) {
            Choice loaded = Stale(original);
            EXPECT_TRUE(LoadBinary(SaveBinary(original), loaded)) << original.index();
            EXPECT_EQ(loaded, original) << original.index();
        }
    }

    TEST(ReflectionVariantTest, MonostateWritesTheIndexOnlyAndLoads) {
        const String xml = SaveXml(Choice{});
        EXPECT_TRUE(HasText(xml, "<choice index=\"0\" />")) << xml.c_str();

        Choice loaded{std::in_place_index<1>, 5};
        XmlLoad load(xml, loaded);
        EXPECT_TRUE(load.loaded);
        EXPECT_EQ(loaded.index(), 0u);
    }

    TEST(ReflectionVariantTest, TwoAlternativesOfOneTypeKeepTheIndex) {
        const Twins second{std::in_place_index<1>, 7};
        const String xml = SaveXml(second);
        EXPECT_TRUE(HasText(xml, "<choice index=\"1\" value=\"7\" />")) << xml.c_str();

        Twins fromXml{std::in_place_index<0>, 0};
        XmlLoad load(xml, fromXml);
        EXPECT_EQ(fromXml.index(), 1u);
        EXPECT_EQ(std::get<1>(fromXml), 7);

        Twins fromBinary{std::in_place_index<0>, 0};
        EXPECT_TRUE(LoadBinary(SaveBinary(second), fromBinary));
        EXPECT_EQ(fromBinary.index(), 1u);
        EXPECT_EQ(std::get<1>(fromBinary), 7);
    }

    // =========================================================================
    // An element of a vector and the value of a map
    // =========================================================================

    TEST(ReflectionVariantTest, AVectorOfVariantsRoundTrips) {
        const ChoiceList original = EveryAlternative();
        const String xml = SaveXml(original);
        EXPECT_TRUE(HasText(xml, "<item index=\"1\" value=\"-42\" />")) << xml.c_str();
        EXPECT_TRUE(HasText(xml, "<item index=\"4\">")) << xml.c_str();

        ChoiceList fromXml{Choice{std::in_place_index<1>, 1}};
        XmlLoad load(xml, fromXml);
        EXPECT_TRUE(load.Reports().empty());
        EXPECT_EQ(fromXml, original);

        ChoiceList fromBinary;
        EXPECT_TRUE(LoadBinary(SaveBinary(original), fromBinary));
        EXPECT_EQ(fromBinary, original);
    }

    TEST(ReflectionVariantTest, AHashMapOfVariantsRoundTrips) {
        ChoiceTable original;
        original["empty"] = Choice{};
        original["number"] = Choice{std::in_place_index<2>, 0.5f};
        original["point"] = Choice{std::in_place_index<4>, MakePoint(1, 2)};
        const String xml = SaveXml(original);
        EXPECT_TRUE(HasText(xml, "<entry key=\"number\" index=\"2\" value=\"0.5\" />")) << xml.c_str();

        ChoiceTable fromXml;
        XmlLoad load(xml, fromXml);
        EXPECT_TRUE(load.Reports().empty());
        EXPECT_EQ(fromXml, original);

        ChoiceTable fromBinary;
        EXPECT_TRUE(LoadBinary(SaveBinary(original), fromBinary));
        EXPECT_EQ(fromBinary, original);
    }

    TEST(ReflectionVariantTest, AMapOfVariantsRoundTrips) {
        ChoiceOrder original;
        original[3] = Choice{std::in_place_index<3>, String("three")};
        original[-1] = Choice{std::in_place_index<1>, 11};
        const String xml = SaveXml(original);
        EXPECT_TRUE(HasText(xml, "<entry key=\"3\" index=\"3\" value=\"three\" />")) << xml.c_str();

        ChoiceOrder fromXml;
        XmlLoad load(xml, fromXml);
        EXPECT_TRUE(load.Reports().empty());
        EXPECT_EQ(fromXml, original);

        ChoiceOrder fromBinary;
        EXPECT_TRUE(LoadBinary(SaveBinary(original), fromBinary));
        EXPECT_EQ(fromBinary, original);
    }

    // =========================================================================
    // A bad index
    // =========================================================================

    TEST(ReflectionVariantTest, AMissingIndexIsAnErrorAndKeepsTheValue) {
        Choice value{std::in_place_index<1>, 5};
        XmlLoad load("<root><State><choice value=\"3\"/></State></root>", value);
        EXPECT_FALSE(load.loaded);
        EXPECT_EQ(load.Reports(), Vector<String>{"State.choice.index: Variant index is missing"});
        EXPECT_EQ(value, (Choice{std::in_place_index<1>, 5}));
    }

    TEST(ReflectionVariantTest, AnIndexEqualToTheCountIsAnErrorAndKeepsTheValue) {
        Choice value{std::in_place_index<1>, 5};
        XmlLoad load(Format("<root><State><choice index=\"{}\" value=\"3\"/></State></root>", kChoiceCount), value);
        EXPECT_FALSE(load.loaded);
        EXPECT_EQ(load.Reports(), Vector<String>{Format("State.choice.index: Invalid variant index {}", kChoiceCount)});
        EXPECT_EQ(value, (Choice{std::in_place_index<1>, 5}));
    }

    TEST(ReflectionVariantTest, ANegativeOrHugeIndexIsAnErrorWithTheNumberOnly) {
        Choice negative{std::in_place_index<1>, 5};
        XmlLoad belowZero("<root><State><choice index=\"-1\" value=\"3\"/></State></root>", negative);
        EXPECT_EQ(belowZero.Reports(), Vector<String>{"State.choice.index: Invalid variant index -1"});
        EXPECT_EQ(negative, (Choice{std::in_place_index<1>, 5}));

        Choice huge{std::in_place_index<1>, 5};
        XmlLoad tooLarge("<root><State><choice index=\"99999999999999999999\" value=\"3\"/></State></root>", huge);
        ASSERT_EQ(tooLarge.Reports().size(), 1u);
        EXPECT_TRUE(HasText(tooLarge.Reports()[0], "State.choice.index: Invalid variant index ")) << tooLarge.Reports()[0].c_str();
        EXPECT_FALSE(HasText(tooLarge.Reports()[0], "99999999999999999999")) << tooLarge.Reports()[0].c_str();
        EXPECT_EQ(huge, (Choice{std::in_place_index<1>, 5}));
    }

    TEST(ReflectionVariantTest, ABadIndexOfAnElementHasTheElementPath) {
        ChoiceList list;
        XmlLoad load("<root><State><choice><item index=\"1\" value=\"2\"/><item index=\"9\"/></choice></State></root>", list);
        EXPECT_EQ(load.Reports(), Vector<String>{"State.choice[1].index: Invalid variant index 9"});
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[0], (Choice{std::in_place_index<1>, 2}));
    }

    TEST(ReflectionVariantTest, AMissingValueKeepsTheDefaultOfTheNamedAlternative) {
        Choice text{std::in_place_index<1>, 5};
        XmlLoad scalar("<root><State><choice index=\"3\"/></State></root>", text);
        EXPECT_FALSE(scalar.loaded);
        EXPECT_TRUE(scalar.Reports().empty());
        EXPECT_EQ(text, (Choice{std::in_place_index<3>, String()}));

        Choice point{std::in_place_index<1>, 5};
        XmlLoad object("<root><State><choice index=\"4\"/></State></root>", point);
        EXPECT_FALSE(object.loaded);
        EXPECT_EQ(point, (Choice{std::in_place_index<4>, Point{}}));
    }

    TEST(ReflectionVariantTest, AStrictReadOfAMalformedIndexIsOneError) {
        XmlDeserializer reader;
        reader.SetStrictValues(true);
        ASSERT_TRUE(reader.LoadFromString("<root><State><choice index=\"one\" value=\"3\"/></State></root>"));
        ASSERT_TRUE(reader.BeginObject("State"));
        Choice value{std::in_place_index<1>, 5};
        EXPECT_FALSE(Ser::Load(reader, value, kKey));
        reader.EndObject();
        ASSERT_EQ(reader.GetErrors().size(), 1u);
        EXPECT_EQ(String(reader.GetErrors()[0].path.ToStringView()), "State.choice.index");
        EXPECT_EQ(value, (Choice{std::in_place_index<1>, 5}));
    }

    // =========================================================================
    // Load diagnostics: `index` and `value` are known keys
    // =========================================================================

    TEST(ReflectionVariantTest, ACleanFileOfEveryShapeHasNoFinding) {
        ChoiceTable table;
        table["point"] = Choice{std::in_place_index<4>, MakePoint(1, 2)};
        table["word"] = Choice{std::in_place_index<3>, String("w")};
        ChoiceOrder order;
        order[1] = Choice{};
        order[2] = Choice{std::in_place_index<2>, 1.5f};
        for (const DiagnosticsMode mode : {DiagnosticsMode::Warn, DiagnosticsMode::Error}) {
            ExpectClean(Choice{std::in_place_index<4>, MakePoint(1, 2)}, mode);
            ExpectClean(Choice{std::in_place_index<1>, 3}, mode);
            ExpectClean(Choice{}, mode);
            ExpectClean(ChoiceList(EveryAlternative()), mode);
            ExpectClean(table, mode);
            ExpectClean(order, mode);
        }
    }

    TEST(ReflectionVariantTest, AStrayAttributeBesideIndexAndValueIsOneFinding) {
        Choice value;
        XmlLoad field("<root><State><choice index=\"1\" value=\"3\" extra=\"x\"/></State></root>", value, DiagnosticsMode::Warn);
        EXPECT_EQ(field.Reports(), Vector<String>{"State.choice.extra: Unknown attribute 'extra'"});
        EXPECT_EQ(value, (Choice{std::in_place_index<1>, 3}));

        ChoiceList list;
        XmlLoad element("<root><State><choice><item index=\"4\" stray=\"1\"><value x=\"1\" y=\"2\"/></item></choice></State></root>", list, DiagnosticsMode::Warn);
        EXPECT_EQ(element.Reports(), Vector<String>{"State.choice[0].stray: Unknown attribute 'stray'"});
    }

}  // namespace BECore::Tests
