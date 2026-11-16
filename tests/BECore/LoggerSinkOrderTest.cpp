#include "LoggerSinkOrderTest.h"

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <Common/ShippedConfig.h>
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

        /// Three sinks of the priorities 0, 50 and 100 with ids 1, 2, 3, listed in the order of @p priorities
        String MakeThreeSinksXml(const int (&priorities)[3]) {
            String xml = "<root><sinks>";
            for (int slot = 0; slot < 3; ++slot) {
                xml += Format("<sink type=\"OrderSink\" id=\"{}\" priority=\"{}\"/>", priorities[slot] / 50 + 1, priorities[slot]);
            }
            xml += "</sinks></root>";
            return xml;
        }

        IntrusivePtrAtomic<LoggerManager> MakeManager(StringView xml = kSinksXml) {
            XmlDeserializer deserializer;
            EXPECT_TRUE(deserializer.LoadFromString(xml));
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

    TEST_F(LoggerSinkOrderTest, AConfigThatIsAlreadyAscendingKeepsItsOrder) {
        const auto manager = MakeManager(MakeThreeSinksXml({0, 50, 100}));

        LogEvent::Emit(LogLevel::Info, OrderSink::kMarker);
        LogEvent::Flush();

        const Vector<int32_t> expected = {1, 2, 3};
        EXPECT_EQ(OrderSink::order, expected);
    }

    TEST_F(LoggerSinkOrderTest, EveryOrderOfTheConfigServesTheLowestPriorityFirst) {
        constexpr int kOrders[6][3] = {{0, 50, 100}, {0, 100, 50}, {50, 0, 100}, {50, 100, 0}, {100, 0, 50}, {100, 50, 0}};
        for (const auto& priorities : kOrders) {
            OrderSink::order.clear();
            const auto manager = MakeManager(MakeThreeSinksXml(priorities));

            LogEvent::Emit(LogLevel::Info, OrderSink::kMarker);
            LogEvent::Flush();

            const Vector<int32_t> expected = {1, 2, 3};
            EXPECT_EQ(OrderSink::order, expected) << "config order " << priorities[0] << ", " << priorities[1] << ", " << priorities[2];
        }
    }

    TEST_F(LoggerSinkOrderTest, TheShippedConfigServesTheEntriesInTheOrderOfTheirPriority) {
        const String text = ReadShippedConfig("LoggerConfig.xml");
        ASSERT_FALSE(text.empty());
        const auto manager = MakeManager(NumberEntries(text, "sink", "OrderSink"));

        LogEvent::Emit(LogLevel::Info, OrderSink::kMarker);
        LogEvent::Flush();

        // File order: OutputSink 1 (priority 0), ConsoleSink 2 (disabled), FileSink 3 (100), LogHistorySink 4 (50)
        const Vector<int32_t> expected = {1, 4, 3};
        EXPECT_EQ(OrderSink::order, expected);
    }

}  // namespace BECore::Tests
