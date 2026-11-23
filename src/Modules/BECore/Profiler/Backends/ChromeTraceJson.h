#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
#include <cstdio>
#include <expected>

namespace BECore {

    /// Appends @p text to @p out as a JSON string literal with its quotes (nullptr is an empty string): the one escaper of every Chrome trace the profiler writes
    inline void AppendJsonString(String& out, const char* text) {
        out.push_back('"');
        for (const char* c = text != nullptr ? text : ""; *c != '\0'; ++c) {
            const auto byte = static_cast<unsigned char>(*c);
            switch (*c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (byte < 0x20) {
                        AppendFormat(out, "\\u{:04x}", byte);
                    } else {
                        out.push_back(*c);
                    }
            }
        }
        out.push_back('"');
    }

    /// A trace file written in pieces, so a trace never has to sit in memory whole. Binary mode; every error names the file and the reason
    class TraceFileWriter {
    public:
        /// Creates or replaces @p path
        [[nodiscard]] static std::expected<TraceFileWriter, String> Open(StringView path);

        TraceFileWriter(TraceFileWriter&& other) noexcept;
        TraceFileWriter& operator=(TraceFileWriter&&) = delete;
        TraceFileWriter(const TraceFileWriter&) = delete;
        TraceFileWriter& operator=(const TraceFileWriter&) = delete;
        ~TraceFileWriter();

        /// Appends @p text; after a refused write the rest is dropped and Close() reports it
        void Write(const String& text);
        /// Closes the file; the error when any write or the close was refused
        [[nodiscard]] std::expected<void, String> Close();

    private:
        TraceFileWriter(std::FILE* file, String fileName);

        std::FILE* _file = nullptr;
        String _fileName;
        bool _failed = false;
    };

    /// Writes @p text to @p path in binary mode; the error names the file and the reason. The one file writer of every Chrome trace the profiler writes
    [[nodiscard]] std::expected<void, String> WriteTextFile(StringView path, const String& text);

    // The event formatters shared by the recorder and the frozen-frame writer. Each appends one event body without the `,\n` separator, which
    // the caller owns, so the two traces cannot drift apart in the events the tools read.

    /// The `M` event that names a thread
    inline void AppendThreadNameEvent(String& out, int pid, uint32_t tid, const char* name) {
        AppendFormat(out, "{{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"name\":", pid, tid);
        AppendJsonString(out, name);
        out.append("}}");
    }

    /// The `M` event that orders a thread's row (lower is higher)
    inline void AppendThreadSortIndexEvent(String& out, int pid, uint32_t tid, uint32_t sortIndex) {
        AppendFormat(out, "{{\"name\":\"thread_sort_index\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"sort_index\":{}}}}}", pid, tid, sortIndex);
    }

    /// The `B` event of a zone at @p timestampUs microseconds, with the site's file and line as args
    inline void AppendZoneBeginEvent(String& out, int pid, uint32_t tid, double timestampUs, const ProfileSite& site) {
        out.append("{\"name\":");
        AppendJsonString(out, site.name);
        AppendFormat(out, ",\"cat\":\"zone\",\"ph\":\"B\",\"pid\":{},\"tid\":{},\"ts\":{:.3f},\"args\":{{\"file\":", pid, tid, timestampUs);
        AppendJsonString(out, site.file);
        AppendFormat(out, ",\"line\":{}}}}}", site.line);
    }

    /// The `E` event of a zone; the inclusive allocation counts are its args only with ENGINE_PROFILER_ALLOC
    inline void AppendZoneEndEvent(String& out, int pid, uint32_t tid, double timestampUs, [[maybe_unused]] uint64_t allocCount, [[maybe_unused]] uint64_t allocBytes) {
        AppendFormat(out, "{{\"ph\":\"E\",\"pid\":{},\"tid\":{},\"ts\":{:.3f}", pid, tid, timestampUs);
        if constexpr (BE_PROFILER_ALLOC != 0) {
            AppendFormat(out, ",\"args\":{{\"allocs\":{},\"bytes\":{}}}", allocCount, allocBytes);
        }
        out.append("}");
    }

}  // namespace BECore
