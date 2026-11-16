#include "AssertHandlerOrderTest.h"

#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/AssertBridge.h>
#include <Common/LogRecorder.h>
#include <Generated/AssertHandlerOrderTest.gen.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace BECore::Tests {

    void OrderHandler::Initialize() {
        UnsubscribeAll();
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

    TEST_F(AssertHandlerOrderTest, TheShippedConfigLogsBeforeItBreaks) {
        const std::filesystem::path file = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "config" / "AssertHandlersConfig.xml";
        std::ifstream stream(file);                                                                          //-NoStd: reads the shipped file as text
        const std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());  //-NoStd: the text of the shipped file
        ASSERT_FALSE(text.empty()) << file.string();

        // The probe of priority 50 goes between the two handlers of the file
        constexpr StringView kEnd = "</handlers>";
        const size_t end = text.find(kEnd.data());
        ASSERT_NE(end, std::string::npos);  //-NoStd: the text of the shipped file
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
