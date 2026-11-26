#pragma once

#include <BECore/File/File.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <cstddef>
#include <cstdint>

namespace BECore {

    CORE_ENUM(ExportLineKind, uint8_t, Plain,
              Step,     ///< a line of the script's own (`== ...`)
              Warning,  ///< of CMake or a compiler
              Error)    ///< of CMake, a compiler, ninja, or the script's failed result

    /// What a line of an export's output is, by the marks the tools write in it
    [[nodiscard]] ExportLineKind ClassifyExportLine(StringView line);

    /// The lines an ExportLog keeps: the newest, since a failed build ends with what went wrong
    inline constexpr size_t kMaxExportLogLines = 5000;

    /// A line is cut here, so output without line ends cannot grow without a bound
    inline constexpr size_t kMaxExportLineBytes = 4096;

    struct ExportLine {
        String text;
        ExportLineKind kind = ExportLineKind::Plain;
    };

    /**
     * @brief The output of an export as classified lines, read from the log file while the process writes it. No ImGui.
     *
     * `Read` takes what was appended since the last call; a read that finds nothing allocates nothing. A line is there once its end has been
     * read; `Finish` takes the rest. The script's own lines (`tools/export/CLAUDE.md`, "Output") give the folder, the reason and a server's address.
     */
    class ExportLog {
    public:
        explicit ExportLog(size_t maxLines = kMaxExportLogLines) : _maxLines(maxLines) {}

        /// Forgets every line and follows the file @p path from its start. It is opened at the first `Read` that finds it, never here:
        /// on Windows an open reader would keep the process from opening the file to write
        void Follow(StringView path);

        void Read();

        /// The writer is gone: reads the rest, takes an unended last line as a line and closes the file
        void Finish();

        [[nodiscard]] size_t GetLineCount() const {
            return _lines.size();
        }

        /// The line @p index of the kept ones, 0 the oldest
        [[nodiscard]] const ExportLine& GetLine(size_t index) const {
            return _lines[(_oldest + index) % _lines.size()];
        }

        /// The lines that are no longer kept
        [[nodiscard]] size_t GetDroppedCount() const {
            return _dropped;
        }

        /// The errors of all lines, the dropped ones too
        [[nodiscard]] uint32_t GetErrorCount() const {
            return _errors;
        }

        [[nodiscard]] uint32_t GetWarningCount() const {
            return _warnings;
        }

        /// The package folder of a successful result line; empty until there is one
        [[nodiscard]] StringView GetResultFolder() const {
            return _resultFolder;
        }

        /// The reason of a failed result line; empty until there is one
        [[nodiscard]] StringView GetFailure() const {
            return _failure;
        }

        /// The address of a server's line; empty until there is one
        [[nodiscard]] StringView GetServeUrl() const {
            return _serveUrl;
        }

        /// Moves when the lines change, and only then
        [[nodiscard]] uint32_t GetRevision() const {
            return _revision;
        }

    private:
        static constexpr size_t kReadBytes = 4096;

        void Take(StringView chunk);
        void CompleteLine();

        File _file;
        String _path;               ///< empty: nothing is followed
        Vector<ExportLine> _lines;  ///< a ring once it holds `_maxLines`
        String _partial;
        String _resultFolder;
        String _failure;
        String _serveUrl;
        size_t _maxLines = 0;
        size_t _oldest = 0;
        size_t _dropped = 0;
        uint32_t _errors = 0;
        uint32_t _warnings = 0;
        uint32_t _revision = 0;
        char _buffer[kReadBytes] = {};
    };

}  // namespace BECore
