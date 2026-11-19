#include "TestListTest.h"

#include <BECore/Config/ConfigFactory.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Tests/TestList.h>
#include <Common/EngineEnvironment.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Common/TestText.h>
#include <Generated/TestListTest.gen.hpp>

namespace BECore::Tests {

    namespace {

        constexpr int32_t kFirstId = 1;
        constexpr int32_t kLastId = 4;

    }  // namespace

    class TestListTest : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }
    };

    TEST_F(TestListTest, ATestListSkipsDisabledEntriesAndDropsUnknownTypes) {
        XmlDeserializer deserializer;
        ASSERT_TRUE(deserializer.LoadFromString(R"(<root><tests>
                                              <test type="ProbeRuntimeTest" id="1" enabled="true"/>
                                              <test type="NoSuchTest" enabled="true"/>
                                              <test type="ProbeRuntimeTest" id="2" enabled="false"/>
                                              <test id="3"/>
                                              <test type="ProbeRuntimeTest" id="4"/>
                                            </tests></root>)"));
        const auto list = New<TestList>();
        list->Deserialize(deserializer);

        ASSERT_EQ(deserializer.GetErrors().size(), 1u);
        const StringView message = deserializer.GetErrors()[0].errorMessage;
        EXPECT_TRUE(Contains(message, "Unknown factory type")) << message.data();
        EXPECT_TRUE(Contains(message, "'NoSuchTest'")) << message.data();
        ASSERT_EQ(list->tests.size(), 2u);
        EXPECT_EQ(Require(list->tests[0]->Cast<ProbeRuntimeTest>()).id, kFirstId);
        EXPECT_EQ(Require(list->tests[1]->Cast<ProbeRuntimeTest>()).id, kLastId);
    }

    TEST_F(TestListTest, TheShippedTestsConfigEnablesTheEngineSmokeTest) {
        const auto list = MakeFromConfig<TestList>("TestsConfig");

        ASSERT_EQ(list->tests.size(), 1u);
        EXPECT_EQ(list->tests[0]->GetName(), "EngineSmokeTest");
    }

}  // namespace BECore::Tests
