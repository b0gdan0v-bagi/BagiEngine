#include "ConsoleLogView.h"

namespace BECore {

    namespace {

        constexpr StringView kTitleBase = "Console";

        char ToLowerAscii(char c) {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool ContainsIgnoreCase(StringView text, StringView needle) {
            if (needle.size() > text.size()) {
                return false;
            }
            const size_t last = text.size() - needle.size();
            for (size_t start = 0; start <= last; ++start) {
                size_t i = 0;
                while (i < needle.size() && ToLowerAscii(text[start + i]) == ToLowerAscii(needle[i])) {
                    ++i;
                }
                if (i == needle.size()) {
                    return true;
                }
            }
            return false;
        }

        void AppendCount(String& out, uint32_t count, StringView singular, StringView plural, bool& first) {
            if (count == 0) {
                return;
            }
            out += first ? " (" : ", ";
            first = false;
            out += Format("{} ", count);
            const StringView noun = count == 1 ? singular : plural;
            out.append(noun.data(), noun.size());
        }

    }  // namespace

    bool ConsoleLogFilter::Matches(const LogRecord& record) const {
        if ((levelMask & LogLevelBit(record.level)) == 0) {
            return false;
        }
        if (category && *category != record.category) {
            return false;
        }
        return text.empty() || ContainsIgnoreCase({record.message.data(), record.message.size()}, {text.data(), text.size()});
    }

    void ConsoleLogView::Update(const LogHistory& history, const ConsoleLogFilter& filter, bool paused) {
        const uint64_t version = history.GetVersion();
        if (_built && version == _historyVersion && filter.revision == _filterRevision && paused == _paused) {
            return;
        }
        if (paused && !_paused) {
            _pauseSequence = history.GetNextSequence();
        }
        _built = true;
        _historyVersion = version;
        _filterRevision = filter.revision;
        _paused = paused;
        ++_rebuildCount;

        _rows.clear();
        for (size_t i = 0; i < history.Size(); ++i) {
            const LogRecord& record = history.At(i);
            if (paused && record.sequence >= _pauseSequence) {
                break;  // sequences grow with the index
            }
            if (filter.Matches(record)) {
                _rows.push_back(static_cast<uint32_t>(i));
            }
        }
    }

    void FormatConsoleTitle(const LogHistory& history, String& out) {
        out.assign(kTitleBase.data(), kTitleBase.size());
        const uint32_t errors = history.GetCount(LogLevel::Error) + history.GetCount(LogLevel::Fatal);
        bool first = true;
        AppendCount(out, errors, "error", "errors", first);
        AppendCount(out, history.GetCount(LogLevel::Warning), "warning", "warnings", first);
        if (!first) {
            out += ")";
        }
    }

}  // namespace BECore
