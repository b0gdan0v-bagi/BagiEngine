#include "LoggerSinkOrderTest.h"

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Generated/LoggerSinkOrderTest.gen.hpp>

namespace BECore::Tests {

    void OrderSink::Initialize() {
        UnsubscribeAll();
        Subscribe<LogEvent, &OrderSink::OnLogEvent>(this);
    }

    void OrderSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void OrderSink::Write(LogLevel level, StringView message) {
        if (ShouldLog(level) && message.find(kMarker) != StringView::npos) {
            order.push_back(id);
        }
    }

    namespace {

        /// Priorities 100, 50, 0 and 50 again: the second 50 comes after the first in the file and must be served after it
        constexpr const char* kSinksXml = R"(<root><sinks>
            <sink type="OrderSink" id="1" priority="100"/>
            <sink type="OrderSink" id="2" priority="50"/>
            <sink type="OrderSink" id="3" priority="0"/>
            <sink type="OrderSink" id="4" priority="50"/>
        </sinks></root>)";

        IntrusivePtrAtomic<LoggerManager> MakeManager() {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(kSinksXml));
            auto manager = New<LoggerManager>();
            manager->Deserialize(deserializer);
            EXPECT_FALSE(deserializer.HasErrors());
            manager->Initialize();
            return manager;
        }

    }  // namespace

    class LoggerSinkOrderTest : public ::testing::Test {
    protected:
        void SetUp() override {
            OrderSink::order.clear();
        }
    };

    TEST_F(LoggerSinkOrderTest, TheLowestPriorityGetsTheLineFirstAndEqualOnesKeepTheConfigOrder) {
        const auto manager = MakeManager();

        LogEvent::Emit(LogLevel::Info, OrderSink::kMarker);
        LogEvent::Flush();

        const Vector<int32_t> expected = {3, 2, 4, 1};
        EXPECT_EQ(OrderSink::order, expected);
    }

    TEST_F(LoggerSinkOrderTest, TheManagerKeepsItsSinksSortedAscending) {
        const auto manager = MakeManager();

        Vector<int> priorities;
        for (const auto& sink : manager->GetSinks()) {
            priorities.push_back(sink->GetPriority());
        }
        const Vector<int> expected = {0, 50, 50, 100};
        EXPECT_EQ(priorities, expected);
    }

}  // namespace BECore::Tests
