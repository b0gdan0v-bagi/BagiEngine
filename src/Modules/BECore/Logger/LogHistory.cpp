#include "LogHistory.h"

#include <BECore/Logger/LogTime.h>

namespace BECore {

    namespace {

        constexpr StringView kGeneralName = "General";
        constexpr StringView kOtherName = "Other";
        constexpr StringView kModulesFolder = "Modules";
        constexpr StringView kSourceFolder = "src";

        bool IsSeparator(char c) {
            return c == '/' || c == '\\';
        }

        /// The component that follows the last component called @p folder and is itself a folder (a separator after it); empty when there is none
        StringView FolderAfterLast(StringView path, StringView folder) {
            StringView found;
            size_t begin = 0;
            while (begin < path.size()) {
                size_t end = begin;
                while (end < path.size() && !IsSeparator(path[end])) {
                    ++end;
                }
                if (path.substr(begin, end - begin) == folder && end < path.size()) {
                    size_t nextEnd = end + 1;
                    while (nextEnd < path.size() && !IsSeparator(path[nextEnd])) {
                        ++nextEnd;
                    }
                    if (nextEnd < path.size() && nextEnd > end + 1) {
                        found = path.substr(end + 1, nextEnd - end - 1);
                    }
                }
                begin = end + 1;
            }
            return found;
        }

    }  // namespace

    StringView LogCategoryOfFile(StringView file) {
        const StringView module = FolderAfterLast(file, kModulesFolder);
        return module.empty() ? FolderAfterLast(file, kSourceFolder) : module;
    }

    StringView FormatLogTimeNow(char (&buffer)[kLogTimeLength]) {
        const LocalLogTime now = GetLocalLogTime();
        const FormatResult written = FormatToN(buffer, sizeof(buffer), "{:02}:{:02}:{:02}.{:03d}", now.tm.tm_hour, now.tm.tm_min, now.tm.tm_sec, now.millisecond);
        return StringView{buffer, std::min(written.size, sizeof(buffer))};
    }

    LogHistory::LogHistory(size_t capacity) : _slots(std::max<size_t>(capacity, 1)) {
        _categories.emplace_back(kGeneralName.data(), kGeneralName.size());
    }

    const LogRecord& LogHistory::At(size_t index) const {
        return _slots[(_head + index) % _slots.size()];
    }

    StringView LogHistory::GetCategoryName(size_t index) const {
        const String& name = _categories[index];
        return {name.data(), name.size()};
    }

    uint16_t LogHistory::ResolveCategory(StringView name) {
        if (name.empty()) {
            return kGeneralLogCategory;
        }
        for (size_t i = 0; i < _categories.size(); ++i) {
            if (GetCategoryName(i) == name) {
                return static_cast<uint16_t>(i);
            }
        }
        // The last place is kept for "Other", so a name past the cap does not grow the set
        if (_categories.size() + 1 < kMaxLogCategories) {
            _categories.emplace_back(name.data(), name.size());
            return static_cast<uint16_t>(_categories.size() - 1);
        }
        for (size_t i = 0; i < _categories.size(); ++i) {
            if (GetCategoryName(i) == kOtherName) {
                return static_cast<uint16_t>(i);
            }
        }
        _categories.emplace_back(kOtherName.data(), kOtherName.size());
        return static_cast<uint16_t>(_categories.size() - 1);
    }

    void LogHistory::Add(LogLevel level, StringView message, LogSite site, StringView timeText) {
        if (_size == _slots.size()) {
            --_counts[static_cast<size_t>(At(0).level)];
            _head = (_head + 1) % _slots.size();
            --_size;
        }

        LogRecord& record = _slots[(_head + _size) % _slots.size()];
        record.sequence = _nextSequence++;
        record.level = level;
        record.site = site;
        record.category = ResolveCategory(site.file != nullptr ? LogCategoryOfFile(site.file) : StringView{});
        const size_t timeLength = std::min(timeText.size(), kLogTimeLength - 1);
        std::memcpy(record.time, timeText.data(), timeLength);
        record.time[timeLength] = '\0';
        record.message.assign(message.data(), message.size());

        ++_size;
        ++_counts[static_cast<size_t>(level)];
        ++_version;
    }

    void LogHistory::Clear() {
        _head = 0;
        _size = 0;
        std::memset(_counts, 0, sizeof(_counts));
        ++_version;
    }

}  // namespace BECore
