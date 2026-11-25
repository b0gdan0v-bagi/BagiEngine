#pragma once

#include <BECore/Logger/LogHistory.h>
#include <BECore/Logger/LogLevel.h>
#include <BECore/Types/Types.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    /// The longest filter text the panel takes
    inline constexpr size_t kMaxConsoleFilterLength = 128;

    /// A bit per LogLevel, all set
    inline constexpr uint32_t kAllLogLevels = (1U << EnumUtils<LogLevel>::Count()) - 1U;

    /// The bit of @p level in ConsoleLogFilter::levelMask
    constexpr uint32_t LogLevelBit(LogLevel level) {
        return 1U << static_cast<uint32_t>(level);
    }

    /// Which lines of a LogHistory the Console lists. Whoever changes a field bumps `revision`, so the view knows to rebuild.
    struct ConsoleLogFilter {
        uint32_t levelMask = kAllLogLevels;
        Optional<uint16_t> category;  ///< an index of LogHistory::GetCategoryName; none: every category
        String text;                  ///< at most kMaxConsoleFilterLength characters; empty matches every line
        uint64_t revision = 0;

        /// The level is on, the category is the wanted one and the message holds `text` (ASCII case-insensitive)
        [[nodiscard]] bool Matches(const LogRecord& record) const;
    };

    /**
     * @brief The rows the Console draws: the indices (for LogHistory::At) of the records that pass a filter.
     *
     * Pure, no ImGui context. The list is a member so it keeps its capacity, and it is made again only when the history, the filter
     * or the pause changed, so a steady frame does no work.
     *
     * A pause freezes the view: lines that arrive after it are not listed, and the history goes on behind it. A line the history
     * drops while paused leaves the rows too, since a row is an index into the history.
     */
    class ConsoleLogView {
    public:
        void Update(const LogHistory& history, const ConsoleLogFilter& filter, bool paused);

        /// Oldest first; valid until the next Update
        [[nodiscard]] const Vector<uint32_t>& GetRows() const {
            return _rows;
        }

        /// How many times Update made the rows again
        [[nodiscard]] uint32_t GetRebuildCount() const {
            return _rebuildCount;
        }

    private:
        Vector<uint32_t> _rows;
        uint64_t _historyVersion = 0;
        uint64_t _filterRevision = 0;
        uint64_t _pauseSequence = 0;  ///< while paused: the sequence the history had when the pause began
        bool _paused = false;
        bool _built = false;
        uint32_t _rebuildCount = 0;
    };

    /// Writes the window title text of the Console into @p out: "Console (3 errors, 5 warnings)" (a level with no lines is left out,
    /// Fatal counts as an error). The id after `###` is added by whoever opens the window, so it holds when the counts change.
    void FormatConsoleTitle(const LogHistory& history, String& out);

}  // namespace BECore
