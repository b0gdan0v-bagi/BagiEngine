#include "ExportLog.h"

namespace BECore {

    namespace {

        constexpr StringView kStepPrefix = "== ";
        constexpr StringView kResultOkPrefix = "== export ok: ";
        constexpr StringView kResultFailedPrefix = "== export failed: ";
        constexpr StringView kErrorMarks[] = {"error:", "error C", "error LNK", "fatal error", "FAILED:", "CMake Error"};
        constexpr StringView kWarningMarks[] = {"warning:", "warning C", "warning LNK", "CMake Warning"};

        bool HoldsAny(StringView line, Span<const StringView> marks) {
            for (const StringView mark : marks) {
                if (line.find(mark) != StringView::npos) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    ExportLineKind ClassifyExportLine(StringView line) {
        if (line.starts_with(kResultFailedPrefix)) {
            return ExportLineKind::Error;
        }
        // The script's own lines quote paths and reasons, which are not marks
        if (line.starts_with(kStepPrefix)) {
            return ExportLineKind::Step;
        }
        if (HoldsAny(line, kErrorMarks)) {
            return ExportLineKind::Error;
        }
        return HoldsAny(line, kWarningMarks) ? ExportLineKind::Warning : ExportLineKind::Plain;
    }

    void ExportLog::Follow(StringView path) {
        _file = File{};
        _path.assign(path.data(), path.size());
        _lines.clear();
        _partial.clear();
        _resultFolder.clear();
        _failure.clear();
        _oldest = 0;
        _dropped = 0;
        _errors = 0;
        _warnings = 0;
        ++_revision;
    }

    void ExportLog::Read() {
        if (_path.empty()) {
            return;
        }
        if (!_file.IsOpen()) {
            auto opened = File::Open(_path, FileMode::Read);
            if (!opened) {
                return;
            }
            _file = std::move(*opened);
        }
        for (;;) {
            const auto count = _file.Read(_buffer, sizeof(_buffer));
            if (!count || *count == 0) {
                return;
            }
            Take(StringView(_buffer, *count));
            if (*count < sizeof(_buffer)) {
                return;
            }
        }
    }

    void ExportLog::Finish() {
        Read();
        if (!_partial.empty()) {
            CompleteLine();
        }
        _file = File{};
        _path.clear();
    }

    void ExportLog::Take(StringView chunk) {
        while (!chunk.empty()) {
            const size_t end = chunk.find('\n');
            const StringView part = chunk.substr(0, end);
            const size_t room = kMaxExportLineBytes - _partial.size();
            _partial.append(part.data(), std::min(part.size(), room));
            if (end == StringView::npos) {
                return;
            }
            CompleteLine();
            chunk.remove_prefix(end + 1);
        }
    }

    void ExportLog::CompleteLine() {
        if (!_partial.empty() && _partial.back() == '\r') {
            _partial.pop_back();
        }
        const StringView text(_partial);
        const ExportLineKind kind = ClassifyExportLine(text);
        _errors += kind == ExportLineKind::Error ? 1 : 0;
        _warnings += kind == ExportLineKind::Warning ? 1 : 0;
        if (text.starts_with(kResultOkPrefix)) {
            const StringView folder = text.substr(kResultOkPrefix.size());
            _resultFolder.assign(folder.data(), folder.size());
        } else if (text.starts_with(kResultFailedPrefix)) {
            const StringView reason = text.substr(kResultFailedPrefix.size());
            _failure.assign(reason.data(), reason.size());
        }

        ExportLine* line = nullptr;
        if (_lines.size() < _maxLines) {
            line = &_lines.emplace_back();
        } else {
            line = &_lines[_oldest];
            _oldest = (_oldest + 1) % _lines.size();
            ++_dropped;
        }
        line->text.assign(_partial.data(), _partial.size());
        line->kind = kind;
        _partial.clear();
        ++_revision;
    }

}  // namespace BECore
