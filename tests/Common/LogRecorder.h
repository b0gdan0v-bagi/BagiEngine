#pragma once

#include <BECore/Logger/LogEvent.h>
#include <BECore/Logger/LogSite.h>
#include <BECore/Types/Types.h>
#include <Events/SubscriptionHolder.h>

namespace BECore::Tests {

    /// Collects the log messages that LogEvent::Flush() delivers. It copies them: the string_view of a LogEvent
    /// points into the ring buffer. Unsubscribes when it goes out of scope.
    class LogRecorder : public SubscriptionHolder {
    public:
        struct Entry {
            LogLevel level = LogLevel::Debug;
            String message;
            LogSite site;
        };

        LogRecorder() {
            // What the ring still holds was logged before this recorder (by an earlier test of the process, too): it is not this recorder's
            LogEvent::Flush();
            Subscribe<LogEvent, &LogRecorder::OnLog>(this);
        }

        /// The first recorded message of @p level that contains every part of @p parts, or nullptr.
        const Entry* Find(LogLevel level, std::initializer_list<StringView> parts) const {
            for (const Entry& entry : _entries) {
                if (entry.level == level && ContainsAll(entry.message, parts)) {
                    return &entry;
                }
            }
            return nullptr;
        }

        /// How many recorded messages of @p level contain every part of @p parts
        size_t Count(LogLevel level, std::initializer_list<StringView> parts) const {
            size_t count = 0;
            for (const Entry& entry : _entries) {
                count += (entry.level == level && ContainsAll(entry.message, parts)) ? 1 : 0;
            }
            return count;
        }

        /// The recorded messages of @p level that contain every part of @p parts, oldest first
        Vector<String> Messages(LogLevel level, std::initializer_list<StringView> parts) const {
            Vector<String> messages;
            for (const Entry& entry : _entries) {
                if (entry.level == level && ContainsAll(entry.message, parts)) {
                    messages.push_back(entry.message);
                }
            }
            return messages;
        }

    private:
        static bool ContainsAll(const String& message, std::initializer_list<StringView> parts) {
            const StringView text(message.data(), message.size());
            for (const StringView part : parts) {
                if (text.find(part) == StringView::npos) {
                    return false;
                }
            }
            return true;
        }

        void OnLog(const LogEvent& event) {
            _entries.push_back({event.level, String(event.message.data(), event.message.size()), event.site});
        }

        Vector<Entry> _entries;
    };

}  // namespace BECore::Tests
