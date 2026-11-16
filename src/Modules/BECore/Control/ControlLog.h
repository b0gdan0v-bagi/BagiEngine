#pragma once

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogHistory.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    /**
     * @brief The warnings and errors of the log as the control API shows them: totals since the start and the last lines.
     *
     * Subscribes to LogEvent itself, so it sees every line whatever the `minLevel` of the sinks. Main thread only, where
     * LogEvent::Flush() delivers; it takes no lock. ControlService owns it (ControlService::Log()); a line is seen once it has been flushed.
     */
    class ControlLog : public SubscriptionHolder {
    public:
        /// Lines of Warning and above kept for log_since
        static constexpr size_t kLogCapacity = 1024;

        /// Most lines one log_since answers with (the newest ones)
        static constexpr size_t kMaxLogSinceLines = 200;

        /// Most `log` events pushed in one frame
        static constexpr uint32_t kMaxLogPushPerFrame = 16;

        /// What ReadSince() found
        struct Since {
            Vector<const LogRecord*> lines;  ///< oldest first; point into the history, valid until the next line is added
            uint64_t warnings = 0;           ///< kept lines since the mark of level Warning, whatever the level filter and the cap
            uint64_t errors = 0;             ///< kept lines since the mark of level Error or Fatal, likewise
            uint64_t dropped = 0;            ///< lines since the mark that the ring has already lost: their level is unknown, so the two counts are lower bounds
            uint64_t next = 0;               ///< the mark to use afterwards
            bool truncated = false;          ///< `lines` holds only the newest ones that matched
        };

        /// What `log_subscribe` asked for and how far the `log` push has got; the frame end hook of the builtins reads and advances it
        struct PushState {
            bool enabled = false;
            LogLevel minLevel = LogLevel::Warning;
            uint64_t cursor = 0;  ///< the sequence of the next line to look at
        };

        /// @p capacity is the number of lines kept; a test passes a small one
        explicit ControlLog(size_t capacity = kLogCapacity);

        /// Subscribes to LogEvent; a second call does nothing. Not in the constructor: the service is built before the event and logger services
        void Start();

        /// Warning lines seen since Start()
        [[nodiscard]] uint64_t GetWarnings() const {
            return _warnings;
        }

        /// Error and Fatal lines seen since Start()
        [[nodiscard]] uint64_t GetErrors() const {
            return _errors;
        }

        /// The mark that selects the lines logged from now on; 0 and 1 both mean "since the start"
        [[nodiscard]] uint64_t GetNextSequence() const {
            return _history.GetNextSequence();
        }

        /// The kept lines
        [[nodiscard]] const LogHistory& History() const {
            return _history;
        }

        [[nodiscard]] PushState& Push() {
            return _push;
        }

        /// The kept lines with `sequence >= mark` of level @p minLevel or above (at most @p maxLines, the newest) and the counts of all the kept lines since @p mark
        [[nodiscard]] Since ReadSince(uint64_t mark, LogLevel minLevel, size_t maxLines) const;

    private:
        void OnLogEvent(const LogEvent& event);

        LogHistory _history;
        PushState _push;
        uint64_t _warnings = 0;
        uint64_t _errors = 0;
        bool _started = false;
    };

}  // namespace BECore
