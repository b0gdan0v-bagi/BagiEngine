#include "FactoryInitializeTest.h"

#include <BECore/Reflection/BinaryDeserializer.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/DataAccessor.h>
#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
#include <Common/TestText.h>
#include <Generated/FactoryInitializeTest.gen.hpp>

namespace BECore::Tests {

    bool InitProbeReads::Initialize(IDeserializer& deserializer) {
        deserializer.ReadAttribute("extra", _extra);
        return true;
    }

    bool InitProbeFails::Initialize(IDeserializer& /*deserializer*/) {
        ++initializeCalls;
        return false;
    }

    namespace {

        constexpr int32_t kFirstId = 1;
        constexpr int32_t kLastId = 4;
        constexpr int32_t kFirstExtra = 11;
        constexpr int32_t kLastExtra = 44;

        /// Loads the XML into a fresh InitProbeList; the deserializer stays with the caller to read its errors
        IntrusivePtrNonAtomic<InitProbeList> Load(XmlDeserializer& deserializer, const char* xml) {
            EXPECT_TRUE(deserializer.LoadFromString(xml));
            auto list = New<InitProbeList>();
            list->Deserialize(deserializer);
            return list;
        }

    }  // namespace

    class FactoryInitializeTest : public ::testing::Test {
    protected:
        void SetUp() override {
            InitProbeFails::initializeCalls = 0;
        }
    };

    TEST_F(FactoryInitializeTest, InitializeReadsAttributesOfItsOwnElement) {
        XmlDeserializer deserializer;
        const auto list = Load(deserializer,
                               R"(<root><probes>
                                    <probe type="InitProbeReads" id="1" extra="11"/>
                                    <probe type="InitProbeReads" id="4" extra="44"/>
                                  </probes></root>)");

        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->probes.size(), 2u);
        // Each element read its own attribute: the hook ran inside the element scope
        EXPECT_EQ(list->probes[0]->GetExtra(), kFirstExtra);
        EXPECT_EQ(list->probes[1]->GetExtra(), kLastExtra);
        EXPECT_EQ(list->probes[0]->Cast<InitProbeReads>()->id, kFirstId);
        EXPECT_EQ(list->probes[1]->Cast<InitProbeReads>()->id, kLastId);
    }

    TEST_F(FactoryInitializeTest, AnElementWhoseInitializeReturnsFalseIsDroppedAndReported) {
        XmlDeserializer deserializer;
        const auto list = Load(deserializer,
                               R"(<root><probes>
                                    <probe type="InitProbeReads" id="1" extra="11"/>
                                    <probe type="InitProbeFails" id="2"/>
                                    <probe type="InitProbePlain" id="3"/>
                                    <probe type="InitProbeReads" id="4" extra="44"/>
                                  </probes></root>)");

        ASSERT_EQ(InitProbeFails::initializeCalls, 1);
        ASSERT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const StringView message = deserializer.GetErrors()[0].errorMessage;
        EXPECT_TRUE(Contains(message, "InitProbeFails")) << message.data();
        EXPECT_TRUE(Contains(message, "failed to initialize")) << message.data();

        // The others stay in order, the failed one is gone
        ASSERT_EQ(list->probes.size(), 3u);
        EXPECT_TRUE(list->probes[0]->Is<InitProbeReads>());
        EXPECT_TRUE(list->probes[1]->Is<InitProbePlain>());
        EXPECT_TRUE(list->probes[2]->Is<InitProbeReads>());
        EXPECT_EQ(list->probes[2]->Cast<InitProbeReads>()->id, kLastId);
    }

    TEST_F(FactoryInitializeTest, ADisabledElementDoesNotCallInitialize) {
        XmlDeserializer deserializer;
        const auto list = Load(deserializer,
                               R"(<root><probes>
                                    <probe type="InitProbeFails" id="2" enabled="false"/>
                                    <probe type="InitProbeReads" id="1" extra="11"/>
                                  </probes></root>)");

        EXPECT_EQ(InitProbeFails::initializeCalls, 0);
        EXPECT_FALSE(deserializer.HasErrors());
        ASSERT_EQ(list->probes.size(), 1u);
        EXPECT_TRUE(list->probes[0]->Is<InitProbeReads>());
    }

    TEST_F(FactoryInitializeTest, ABaseWithoutTheMethodLoadsAsBefore) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(
            R"(<root><items>
                 <nohook type="NoHookProbeImpl" value="7"/>
                 <nohook type="Missing" value="8"/>
               </items></root>)"));
        const auto list = New<NoHookProbeList>();
        list->Deserialize(deserializer);

        // The unknown type is reported (and falls back to the abstract base: dropped), the known one stays
        EXPECT_TRUE(deserializer.HasErrors());
        ASSERT_EQ(list->items.size(), 1u);
        EXPECT_EQ(list->items[0]->GetValue(), 7);
    }

    namespace {

        // A factory vector is read through the factory only where FactoryTraits<InitProbe> (this file's .gen.hpp) is visible:
        // a file without it compiles the same Ser:: templates on the non-factory path, an ODR clash that Release inlines
        static_assert(HasFactory<InitProbe>);

        using ByteBuffer = Vector<uint8_t>;

        constexpr StringView kDroppedItems = "Data lost, the fixed container is full: kept 2 of 4 items";

        void ExpectOneDropReport(const IDeserializer& deserializer, StringView field, StringView message) {
            ASSERT_TRUE(deserializer.HasErrors());
            ASSERT_EQ(deserializer.GetErrors().size(), 1u);
            EXPECT_EQ(deserializer.GetErrors()[0].path, field);
            EXPECT_EQ(deserializer.GetErrors()[0].errorMessage, message);
        }

        template <typename Value>
        ByteBuffer SaveBinaryField(const Value& value) {
            BinarySerializer serializer;
            if (serializer.BeginObject("State")) {
                Ser::Save(serializer, value, "value");
                serializer.EndObject();
            }
            return serializer.GetData();
        }

        template <typename Value>
        void LoadXmlField(XmlDeserializer& deserializer, const char* xml, Value& value) {
            ASSERT_TRUE(deserializer.LoadFromString(xml));
            ASSERT_TRUE(deserializer.BeginObject("State"));
            Ser::Load(deserializer, value, "value");
            deserializer.EndObject();
        }

        template <typename Value>
        void LoadBinaryField(BinaryDeserializer& deserializer, const ByteBuffer& data, Value& value) {
            ASSERT_TRUE(deserializer.LoadFromBuffer(data));
            ASSERT_TRUE(deserializer.BeginObject("State"));
            Ser::Load(deserializer, value, "value");
            deserializer.EndObject();
        }

    }  // namespace

    TEST_F(FactoryInitializeTest, MoreFactoryItemsThanAFixedVectorHoldsAreCutAndReportedFromXml) {
        FixedVector<IntrusivePtrNonAtomic<InitProbe>, 2> probes;
        XmlDeserializer deserializer;
        LoadXmlField(deserializer,
                     "<root><State><value><probe type=\"InitProbePlain\" id=\"1\"/><probe type=\"InitProbePlain\" id=\"2\"/>"
                     "<probe type=\"InitProbePlain\" id=\"3\"/><probe type=\"InitProbePlain\" id=\"4\"/></value></State></root>",
                     probes);
        ASSERT_EQ(probes.size(), 2u);
        EXPECT_TRUE(probes[0]->Is<InitProbePlain>());
        ExpectOneDropReport(deserializer, "State.value", kDroppedItems);
    }

    TEST_F(FactoryInitializeTest, MoreFactoryItemsThanAFixedVectorHoldsAreCutAndReportedFromBinary) {
        Vector<IntrusivePtrNonAtomic<InitProbe>> roomy;
        for (int32_t i = 0; i < 4; ++i) {
            roomy.push_back(New<InitProbePlain>());
        }
        FixedVector<IntrusivePtrNonAtomic<InitProbe>, 2> probes;
        BinaryDeserializer deserializer;
        LoadBinaryField(deserializer, SaveBinaryField(roomy), probes);
        ASSERT_EQ(probes.size(), 2u);
        EXPECT_TRUE(probes[1]->Is<InitProbePlain>());
        ExpectOneDropReport(deserializer, "State.value", kDroppedItems);
    }

}  // namespace BECore::Tests
