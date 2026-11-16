#include "AssertHandlerOrderTest.h"

#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AssertBridge.h>
#include <Common/LogRecorder.h>
#include <Common/ShippedConfig.h>
#include <Generated/AssertHandlerOrderTest.gen.hpp>

namespace BECore::Tests {

    void OrderHandler::Initialize() {
        Disconnect();
        Subscribe<AssertEvent, &OrderHandler::OnAssert>(this);
    }

    void OrderHandler::OnAssert(const AssertEvent&) {
        if (flushLog) {
            LogEvent::Flush();
            if (afterFlush != nullptr) {
                afterFlush();
            }
        }
        order.push_back(id);
    }

    namespace {

        constexpr const char* kExpression = "AssertHandlerOrderTest expression";

        /// Priorities 100, 50, 0 and 50 again: the second 50 comes after the first in the file and must run after it
        constexpr const char* kHandlersXml = R"(<root><handlers>
            <handler type="OrderHandler" id="1" priority="100"/>
            <handler type="OrderHandler" id="2" priority="50"/>
            <handler type="OrderHandler" id="3" priority="0"/>
            <handler type="OrderHandler" id="4" priority="50"/>
        </handlers></root>)";

        /// A manager of the handlers of @p xml, initialised: its handlers are subscribed until it is released
        IntrusivePtrAtomic<AssertHandlerManager> MakeManager(StringView xml) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(String(xml)));
            auto manager = New<AssertHandlerManager>();
            manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            manager->Initialize();
            return manager;
        }

        /// Three handlers of the priorities 0, 50 and 100 with ids 1, 2, 3, listed in the order of @p priorities
        String MakeThreeHandlersXml(const int (&priorities)[3]) {
            String xml = "<root><handlers>";
            for (int slot = 0; slot < 3; ++slot) {
                xml += Format("<handler type=\"OrderHandler\" id=\"{}\" priority=\"{}\"/>", priorities[slot] / 50 + 1, priorities[slot]);
            }
            xml += "</handlers></root>";
            return xml;
        }

        void EmitAnAssert(AssertType type) {
            AssertBridge::Capture capture;
            AssertEvent::Emit(__FILE__, __LINE__, kExpression, nullptr, type);
        }

        const LogRecorder* g_recorder = nullptr;
        bool g_logLineSeen = false;

        void LookForTheLogLine() {
            g_logLineSeen = g_recorder->Find(LogLevel::Warning, {kExpression}) != nullptr;
        }

    }  // namespace

    class AssertHandlerOrderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            OrderHandler::order.clear();
            OrderHandler::afterFlush = nullptr;
        }

        void TearDown() override {
            OrderHandler::afterFlush = nullptr;
        }
    };

    TEST_F(AssertHandlerOrderTest, TheLowestPriorityRunsFirstAndEqualOnesKeepTheConfigOrder) {
        const auto manager = MakeManager(kHandlersXml);

        EmitAnAssert(AssertType::Assert);

        const Vector<int32_t> expected = {3, 2, 4, 1};
        EXPECT_EQ(OrderHandler::order, expected);
    }

    TEST_F(AssertHandlerOrderTest, TheManagerKeepsItsHandlersSortedAscending) {
        const auto manager = MakeManager(kHandlersXml);

        Vector<int> priorities;
        for (const auto& handler : manager->GetHandlers()) {
            priorities.push_back(handler->GetPriority());
        }
        const Vector<int> expected = {0, 50, 50, 100};
        EXPECT_EQ(priorities, expected);
    }

    TEST_F(AssertHandlerOrderTest, AConfigThatIsAlreadyAscendingKeepsItsOrder) {
        const auto manager = MakeManager(MakeThreeHandlersXml({0, 50, 100}));

        EmitAnAssert(AssertType::Assert);

        const Vector<int32_t> expected = {1, 2, 3};
        EXPECT_EQ(OrderHandler::order, expected);
    }

    TEST_F(AssertHandlerOrderTest, ADisconnectedHandlerGetsNoAssert) {
        const auto manager = MakeManager(MakeThreeHandlersXml({0, 50, 100}));
        for (const auto& handler : manager->GetHandlers()) {
            if (handler->GetPriority() == 50) {
                handler->Disconnect();
            }
        }

        EmitAnAssert(AssertType::Assert);

        const Vector<int32_t> expected = {1, 3};
        EXPECT_EQ(OrderHandler::order, expected);
    }

    TEST_F(AssertHandlerOrderTest, EveryOrderOfTheConfigRunsTheLowestPriorityFirst) {
        constexpr int kOrders[6][3] = {{0, 50, 100}, {0, 100, 50}, {50, 0, 100}, {50, 100, 0}, {100, 0, 50}, {100, 50, 0}};
        for (const auto& priorities : kOrders) {
            OrderHandler::order.clear();
            const auto manager = MakeManager(MakeThreeHandlersXml(priorities));

            EmitAnAssert(AssertType::Assert);

            const Vector<int32_t> expected = {1, 2, 3};
            EXPECT_EQ(OrderHandler::order, expected) << "config order " << priorities[0] << ", " << priorities[1] << ", " << priorities[2];
        }
    }

    TEST_F(AssertHandlerOrderTest, TheShippedConfigRunsTheEntriesInTheOrderOfTheirPriority) {
        const String text = ReadShippedConfig("AssertHandlersConfig.xml");
        ASSERT_FALSE(text.empty());
        const auto manager = MakeManager(NumberEntries(text, "handler", "OrderHandler"));

        EmitAnAssert(AssertType::Assert);

        // LogHandler (priority 0) is the first entry of the file, DebugBreakHandler (100) the second
        const Vector<int32_t> expected = {1, 2};
        EXPECT_EQ(OrderHandler::order, expected);
    }

    TEST_F(AssertHandlerOrderTest, TheShippedConfigLogsBeforeItBreaks) {
        const String text = ReadShippedConfig("AssertHandlersConfig.xml");
        ASSERT_FALSE(text.empty());

        // The probe of priority 50 goes between the two handlers of the file
        constexpr StringView kEnd = "</handlers>";
        const size_t end = text.find(kEnd.data());
        ASSERT_NE(end, String::npos);
        String xml(text.data(), end);
        xml += R"(<handler type="OrderHandler" id="9" priority="50" flushLog="true"/>)";
        xml += String(text.data() + end, text.size() - end);
        const auto manager = MakeManager(xml);
        ASSERT_NE(manager->GetHandler<DebugBreakHandler>(), nullptr);
        manager->GetHandler<DebugBreakHandler>()->SetEnabled(false);

        LogRecorder recorder;
        g_recorder = &recorder;
        g_logLineSeen = false;
        OrderHandler::afterFlush = &LookForTheLogLine;

        EmitAnAssert(AssertType::Expect);
        g_recorder = nullptr;

        EXPECT_TRUE(g_logLineSeen) << "the LogHandler of priority 0 must have written its line before the handler of priority 50";
    }

}  // namespace BECore::Tests
