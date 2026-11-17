#include "AssetPurposeTest.h"

#include <BECore/Reflection/AbstractFactory.h>
#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <Generated/AssetPurposeTest.gen.hpp>
#include <gtest/gtest.h>

namespace BECore::Tests {

    using namespace AssetPurposeTestTypes;

    TEST(AssetPurpose, TheOptionIsCarriedByTheStaticMetaAndTheTraits) {
        EXPECT_EQ(PurposeSimProbe::GetStaticTypeMeta().purpose, AssetPurpose::Sim);
        EXPECT_EQ(PurposeVisualProbe::GetStaticTypeMeta().purpose, AssetPurpose::Visual);
        EXPECT_EQ(PurposeEditorProbe::GetStaticTypeMeta().purpose, AssetPurpose::Editor);
        EXPECT_EQ(ReflectionTraits<PurposeSimProbe>::purpose, AssetPurpose::Sim);
        EXPECT_EQ(ReflectionTraits<PurposeVisualProbe>::purpose, AssetPurpose::Visual);
        EXPECT_EQ(ReflectionTraits<PurposeEditorProbe>::purpose, AssetPurpose::Editor);
    }

    TEST(AssetPurpose, AClassWithoutTheOptionIsCommon) {
        EXPECT_EQ(PurposeCommonProbe::GetStaticTypeMeta().purpose, AssetPurpose::Common);
        EXPECT_EQ(ReflectionTraits<PurposeCommonProbe>::purpose, AssetPurpose::Common);
    }

    TEST(AssetPurpose, ThePurposeIsReadThroughTheVirtualMeta) {
        const PurposeVisualProbe probe;
        EXPECT_EQ(probe.GetTypeMeta().purpose, AssetPurpose::Visual);
    }

    TEST(AssetPurpose, AMetaMadeByHandIsCommon) {
        constexpr ClassMeta meta{1, "X"};
        EXPECT_EQ(meta.purpose, AssetPurpose::Common);
    }

    TEST(AssetPurpose, NamesParseBack) {
        EXPECT_EQ(PurposeName(AssetPurpose::Sim), "sim");
        EXPECT_EQ(PurposeName(AssetPurpose::Visual), "visual");
        EXPECT_EQ(PurposeName(AssetPurpose::Editor), "editor");
        EXPECT_EQ(TryParsePurpose("sim"), AssetPurpose::Sim);
        EXPECT_EQ(TryParsePurpose("visual"), AssetPurpose::Visual);
        EXPECT_EQ(TryParsePurpose("editor"), AssetPurpose::Editor);
        EXPECT_FALSE(TryParsePurpose("common").has_value());
        EXPECT_FALSE(TryParsePurpose("Sim").has_value());
        EXPECT_FALSE(TryParsePurpose("").has_value());
    }

    TEST(AssetPurpose, IsAllowedInTable) {
        constexpr AssetPurpose kTypes[] = {AssetPurpose::Common, AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor};
        constexpr AssetPurpose kParts[] = {AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor};
        for (const AssetPurpose type : kTypes) {
            for (const AssetPurpose part : kParts) {
                const bool expected = type == AssetPurpose::Common || type == part;
                EXPECT_EQ(IsAllowedIn(type, part), expected) << PurposeName(type).data() << " in " << PurposeName(part).data();
            }
        }
    }

    namespace {

        constexpr int32_t kSimValue = 11;
        constexpr int32_t kVisualValue = 22;
        constexpr int32_t kCommonValue = 33;
        constexpr int32_t kSimElementValue = 44;
        constexpr int32_t kVisualElementValue = 55;

        /// The same content for both formats: a sim, a hidden visual and a common field, a factory vector with a sim, a
        /// visual and a sim element, and a single factory pointer holding a visual element
        PurposeSimHolder MakeContent() {
            PurposeSimHolder holder;
            holder.simPart.value = kSimValue;
            holder.visualPart.value = kVisualValue;
            holder.commonPart.value = kCommonValue;
            auto sim = New<PurposeSimDerived>();
            sim->value = kSimElementValue;
            auto visual = New<PurposeVisualDerived>();
            visual->value = kVisualElementValue;
            holder.elements.push_back(sim);
            holder.elements.push_back(visual);
            holder.elements.push_back(New<PurposeSimDerived>());
            holder.single = visual;
            return holder;
        }

        void ResetCounters() {
            PurposeSimDerived::constructed = 0;
            PurposeVisualDerived::constructed = 0;
        }

        bool Contains(StringView text, StringView part) {
            return text.find(part) != StringView::npos;
        }

        template <typename Deserializer>
        void ExpectOneRefusal(const Deserializer& deserializer, StringView path, StringView message) {
            ASSERT_EQ(deserializer.GetErrors().size(), 1u);
            EXPECT_EQ(deserializer.GetErrors()[0].path, path);
            EXPECT_EQ(deserializer.GetErrors()[0].errorMessage, message);
        }

        /// What a loader gets in `holder` when the filter refuses nothing but the visual field and the visual elements
        void ExpectSimPartOnly(const PurposeSimHolder& holder) {
            EXPECT_EQ(holder.simPart.value, kSimValue);
            EXPECT_EQ(holder.commonPart.value, kCommonValue);
            EXPECT_EQ(holder.visualPart.value, 0);
            ASSERT_EQ(holder.elements.size(), 2u);
            EXPECT_EQ(holder.elements[0]->Cast<PurposeSimDerived>()->value, kSimElementValue);
            EXPECT_EQ(holder.elements[1]->Cast<PurposeSimDerived>()->value, 0);
            EXPECT_FALSE(holder.single);
            EXPECT_EQ(PurposeVisualDerived::constructed, 0);
        }

        constexpr const char* kXml = R"(<root>
            <simPart value="11"/>
            <visualPart value="22"/>
            <commonPart value="33"/>
            <elements>
                <element type="PurposeSimDerived" value="44"/>
                <element type="PurposeVisualDerived" value="55"/>
                <element type="PurposeSimDerived"/>
            </elements>
            <single type="PurposeVisualDerived" value="55"/>
        </root>)";

    }  // namespace

    class AssetPurposeFilter : public ::testing::Test {
    protected:
        void SetUp() override {
            ResetCounters();
        }
    };

    TEST_F(AssetPurposeFilter, SimFilterLoadsTheSimAndCommonDataOfAnXmlDocumentAndRefusesTheRest) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kXml));
        deserializer.SetPurposeFilter(AssetPurpose::Sim);

        PurposeSimHolder holder;
        holder.Deserialize(deserializer);

        ExpectSimPartOnly(holder);
        ASSERT_EQ(deserializer.GetErrors().size(), 3u);
        EXPECT_EQ(deserializer.GetErrors()[0].path, "visualPart");
        EXPECT_EQ(deserializer.GetErrors()[0].errorMessage, "Type 'PurposeVisualProbe' (purpose visual) is not allowed in the sim part");
        EXPECT_TRUE(Contains(deserializer.GetErrors()[1].path, "elements")) << deserializer.GetErrors()[1].path.data();
        EXPECT_EQ(deserializer.GetErrors()[1].errorMessage, "Type 'PurposeVisualDerived' (purpose visual) is not allowed in the sim part");
        EXPECT_EQ(deserializer.GetErrors()[2].path, "single");
    }

    TEST_F(AssetPurposeFilter, WithoutAFilterTheSameDocumentLoadsWholeAsBefore) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kXml));
        EXPECT_FALSE(deserializer.GetPurposeFilter().has_value());

        PurposeSimHolder holder;
        holder.Deserialize(deserializer);

        EXPECT_FALSE(deserializer.HasErrors());
        EXPECT_EQ(holder.visualPart.value, kVisualValue);
        ASSERT_EQ(holder.elements.size(), 3u);
        EXPECT_EQ(holder.elements[1]->Cast<PurposeVisualDerived>()->value, kVisualElementValue);
        ASSERT_TRUE(holder.single);
        EXPECT_EQ(holder.single->Cast<PurposeVisualDerived>()->value, kVisualElementValue);
        EXPECT_EQ(PurposeVisualDerived::constructed, 2);
    }

    TEST_F(AssetPurposeFilter, TheSameHoldsForABinaryDocument) {
        BinarySerializer serializer;
        MakeContent().Serialize(serializer);
        ResetCounters();

        BinaryDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromBuffer(serializer.GetData()));
        deserializer.SetPurposeFilter(AssetPurpose::Sim);

        PurposeSimHolder holder;
        holder.Deserialize(deserializer);

        ExpectSimPartOnly(holder);
        EXPECT_EQ(deserializer.GetErrors().size(), 3u);
        EXPECT_EQ(deserializer.GetErrors()[0].path, "visualPart");

        BinaryDeserializer whole;
        ASSERT_TRUE(whole.LoadFromBuffer(serializer.GetData()));
        PurposeSimHolder everything;
        everything.Deserialize(whole);
        EXPECT_FALSE(whole.HasErrors());
        EXPECT_EQ(everything.visualPart.value, kVisualValue);
        EXPECT_EQ(everything.elements.size(), 3u);
        EXPECT_TRUE(everything.single);
    }

    TEST_F(AssetPurposeFilter, ARefusedClassReadsNoFieldAtItsRoot) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(kXml));
        deserializer.SetPurposeFilter(AssetPurpose::Visual);

        PurposeSimHolder holder;
        holder.Deserialize(deserializer);

        EXPECT_EQ(holder.simPart.value, 0);
        EXPECT_EQ(holder.commonPart.value, 0);
        EXPECT_TRUE(holder.elements.empty());
        ExpectOneRefusal(deserializer, "", "Type 'PurposeSimHolder' (purpose sim) is not allowed in the visual part");
    }

    TEST_F(AssetPurposeFilter, ACommonTypeIsAcceptedInEveryPart) {
        for (const AssetPurpose part : {AssetPurpose::Sim, AssetPurpose::Visual, AssetPurpose::Editor}) {
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(R"(<root><commonPart value="33"/></root>)"));
            deserializer.SetPurposeFilter(part);
            PurposeCommonHolder holder;
            holder.Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors()) << PurposeName(part).data();
            EXPECT_EQ(holder.commonPart.value, kCommonValue);
        }
    }

    TEST_F(AssetPurposeFilter, AcceptsPurposeAnswersAndReportsAtTheCurrentPath) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString("<root><a><b/></a></root>"));
        EXPECT_TRUE(deserializer.AcceptsPurpose(AssetPurpose::Visual, "X"));
        EXPECT_FALSE(deserializer.HasErrors());

        deserializer.SetPurposeFilter(AssetPurpose::Sim);
        ASSERT_TRUE(deserializer.BeginObject("a"));
        EXPECT_TRUE(deserializer.AcceptsPurpose(AssetPurpose::Sim, "X"));
        EXPECT_TRUE(deserializer.AcceptsPurpose(AssetPurpose::Common, "X"));
        EXPECT_FALSE(deserializer.AcceptsPurpose(AssetPurpose::Editor, "X"));
        deserializer.EndObject();
        ExpectOneRefusal(deserializer, "a", "Type 'X' (purpose editor) is not allowed in the sim part");

        deserializer.SetPurposeFilter({});
        EXPECT_TRUE(deserializer.AcceptsPurpose(AssetPurpose::Editor, "X"));
    }

    TEST_F(AssetPurposeFilter, TheFilterSurvivesLoadingAnotherDocumentAndFactoryMetaIsFound) {
        XmlDeserializer deserializer;
        deserializer.SetPurposeFilter(AssetPurpose::Sim);
        ASSERT_TRUE(deserializer.LoadFromString("<root/>"));
        EXPECT_EQ(deserializer.GetPurposeFilter(), AssetPurpose::Sim);

        const auto& factory = AbstractFactory<PurposeAssetBase>::GetInstance();
        ASSERT_NE(factory.FindMeta("PurposeVisualDerived"), nullptr);
        EXPECT_EQ(factory.FindMeta("PurposeVisualDerived")->purpose, AssetPurpose::Visual);
        EXPECT_EQ(factory.FindMeta("NoSuchType"), nullptr);
    }

}  // namespace BECore::Tests
