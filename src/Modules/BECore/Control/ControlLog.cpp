#include "ControlLog.h"

namespace BECore {

    ControlLog::ControlLog(size_t capacity) : _history(capacity) {}

    void ControlLog::Start() {
        if (_started) {
            return;
        }
        Subscribe<LogEvent, &ControlLog::OnLogEvent>(this);
        _started = true;
    }

    void ControlLog::OnLogEvent(const LogEvent& event) {
        if (event.level < LogLevel::Warning) {
            return;
        }
        if (event.level == LogLevel::Warning) {
            ++_warnings;
        } else {
            ++_errors;
        }
        char time[kLogTimeLength];
        _history.Add(event.level, event.message, event.site, FormatLogTimeNow(time));
    }

    ControlLog::Since ControlLog::ReadSince(uint64_t mark, LogLevel minLevel, size_t maxLines) const {
        Since since;
        since.next = _history.GetNextSequence();

        // The sequences kept are consecutive, so the line at index i has the sequence `oldest + i`
        const uint64_t oldest = since.next - _history.Size();
        const uint64_t wanted = std::max<uint64_t>(mark, 1);
        since.dropped = wanted < oldest ? oldest - wanted : 0;

        for (uint64_t sequence = std::max(wanted, oldest); sequence < since.next; ++sequence) {
            const LogRecord& record = _history.At(static_cast<size_t>(sequence - oldest));
            if (record.level == LogLevel::Warning) {
                ++since.warnings;
            } else {
                ++since.errors;
            }
            if (record.level >= minLevel) {
                since.lines.push_back(&record);
            }
        }

        if (since.lines.size() > maxLines) {
            since.lines.erase(since.lines.begin(), since.lines.end() - static_cast<ptrdiff_t>(maxLines));
            since.truncated = true;
        }
        return since;
    }

}  // namespace BECore
