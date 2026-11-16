#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>

namespace BECore::Tests {

    /// A sink that writes its `id` to a static list when it gets the line of LoggerSinkOrderTest, so a test reads the order the sinks were served in
    class OrderSink : public ILogSink {
        BE_CLASS(OrderSink)

    public:
        OrderSink() = default;
        ~OrderSink() override = default;

        void Initialize() override;
        void Write(LogLevel level, StringView message) override;

        BE_REFLECT_FIELD int32_t id = 0;

        static inline Vector<int32_t> order;

        /// The text of the line a test logs; the lines of other tests and of the engine's own sinks pass through the same event
        static constexpr StringView kMarker = "LoggerSinkOrderTest line";

    private:
        void OnLogEvent(const LogEvent& event);
    };

}  // namespace BECore::Tests
