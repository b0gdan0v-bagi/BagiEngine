#include "FactoryInitializeTest.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/Printers.h>
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

        bool Contains(eastl::string_view text, eastl::string_view part) {
            return text.find(part) != eastl::string_view::npos;
        }

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
        const eastl::string_view message = deserializer.GetErrors()[0].errorMessage.ToStringView();
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

}  // namespace BECore::Tests
