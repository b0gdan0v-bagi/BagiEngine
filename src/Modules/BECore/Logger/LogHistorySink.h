#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogHistory.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Keeps the last log lines in a LogHistory for a viewer (the editor's Console panel).
     *
     * Main thread only: LogEvent::Flush() delivers the lines there, and the panel reads and clears the history on the same thread,
     * so the history takes no lock. The lock-free side of the Logger is the ring buffer in front of it.
     */
    class LogHistorySink : public ILogSink {
        BE_CLASS(LogHistorySink)

    public:
        /// The range `capacity` is clamped to when the history is built
        static constexpr int kMinCapacity = 64;
        static constexpr int kMaxCapacity = 65536;
        /// The capacity of a sink whose config names none
        static constexpr int kDefaultCapacity = 2048;

        LogHistorySink() = default;
        ~LogHistorySink() override = default;

        void Initialize() override;

        /// Records a line without a site
        void Write(LogLevel level, StringView message) override;

        /// nullptr until Initialize() has run
        LogHistory* GetHistory() {
            return _history.get();
        }
        const LogHistory* GetHistory() const {
            return _history.get();
        }

    private:
        void OnLogEvent(const LogEvent& event);

        void Record(LogLevel level, StringView message, LogSite site);

        void SetCapacity(int capacity) {
            _capacity = capacity;
        }
        int GetCapacity() const {
            return _capacity;
        }

    private:
        BE_REFLECT_FIELD int _capacity = kDefaultCapacity;
        UniquePtr<LogHistory> _history;
        bool _initialized = false;
    };

}  // namespace BECore
