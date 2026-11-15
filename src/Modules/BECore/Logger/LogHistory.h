#pragma once

#include <BECore/Logger/LogLevel.h>
#include <BECore/Logger/LogSite.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// "HH:MM:SS.mmm" and the terminating zero
    inline constexpr size_t kLogTimeLength = 13;

    /// The categories a history keeps, "General" and "Other" included: the folders of a project are few, but a path is not an identifier, so the set stays bounded
    inline constexpr uint16_t kMaxLogCategories = 64;

    /// The category of a line that has no site or whose site names no module
    inline constexpr uint16_t kGeneralLogCategory = 0;

    /// The module folder of a source path (`/` or `\`): the component after the last `Modules/`, else the one after the last `src/`;
    /// empty when the path has neither, or when nothing but a file follows. The view points into @p file.
    StringView LogCategoryOfFile(StringView file);

    struct LogRecord {
        uint64_t sequence = 0;  ///< from 1, never reused: a pause remembers the first sequence it must not show
        LogLevel level = LogLevel::Debug;
        LogSite site;
        uint16_t category = kGeneralLogCategory;  ///< an index of LogHistory::GetCategoryName
        char time[kLogTimeLength] = {};
        String message;
    };

    /**
     * @brief The last `capacity` log lines, oldest first, with a count per level of the lines kept.
     *
     * Plain data for a log view: no thread safety (one thread adds, the main one, where LogEvent::Flush delivers lines), no ImGui, no
     * engine services. The storage is a ring of slots made once and a line is copied into the oldest one, so a full history
     * allocates only for a message longer than the slot's string has held before, and never grows past `capacity` records.
     */
    class LogHistory {
    public:
        /// A capacity of 0 is 1
        explicit LogHistory(size_t capacity);

        /// Copies the line; drops the oldest record when the history is full. A @p timeText longer than kLogTimeLength - 1 is cut.
        void Add(LogLevel level, StringView message, LogSite site, StringView timeText);

        /// Empties the history. The sequence goes on and the categories seen are kept.
        void Clear();

        size_t Size() const {
            return _size;
        }

        size_t GetCapacity() const {
            return _slots.size();
        }

        /// @p index 0 is the oldest record kept; below Size()
        const LogRecord& At(size_t index) const;

        /// Grows on every Add and Clear, and on nothing else
        uint64_t GetVersion() const {
            return _version;
        }

        /// How many of the records kept have this level
        uint32_t GetCount(LogLevel level) const {
            return _counts[static_cast<size_t>(level)];
        }

        /// The sequence the next record gets
        uint64_t GetNextSequence() const {
            return _nextSequence;
        }

        size_t GetCategoryCount() const {
            return _categories.size();
        }

        /// @p index below GetCategoryCount(); 0 is "General"
        StringView GetCategoryName(size_t index) const;

    private:
        uint16_t ResolveCategory(StringView name);

        Vector<LogRecord> _slots;
        Vector<String> _categories;
        size_t _head = 0;
        size_t _size = 0;
        uint64_t _version = 0;
        uint64_t _nextSequence = 1;
        uint32_t _counts[EnumUtils<LogLevel>::Count()] = {};
    };

}  // namespace BECore
