#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Profiler/ProfileSite.h>
#include <BECore/Types/Types.h>
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
                        out.append(Format("\\u{:04x}", byte));
                    } else {
                        out.push_back(*c);
                    }
            }
        }
        out.push_back('"');
    }

    /// Writes @p text to @p path in binary mode; the error names the file and the reason. The one file writer of every Chrome trace the profiler writes
    [[nodiscard]] std::expected<void, String> WriteTextFile(StringView path, const String& text);

    // The event formatters shared by the recorder and the frozen-frame writer. Each appends one event body without the `,\n` separator, which
    // the caller owns, so the two traces cannot drift apart in the events the tools read.

    /// The `M` event that names a thread
    inline void AppendThreadNameEvent(String& out, int pid, uint32_t tid, const char* name) {
        out.append(Format("{{\"name\":\"thread_name\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"name\":", pid, tid));
        AppendJsonString(out, name);
        out.append("}}");
    }

    /// The `M` event that orders a thread's row (lower is higher)
    inline void AppendThreadSortIndexEvent(String& out, int pid, uint32_t tid, uint32_t sortIndex) {
        out.append(Format("{{\"name\":\"thread_sort_index\",\"ph\":\"M\",\"pid\":{},\"tid\":{},\"args\":{{\"sort_index\":{}}}}}", pid, tid, sortIndex));
    }

    /// The `B` event of a zone at @p timestampUs microseconds, with the site's file and line as args
    inline void AppendZoneBeginEvent(String& out, int pid, uint32_t tid, double timestampUs, const ProfileSite& site) {
        out.append("{\"name\":");
        AppendJsonString(out, site.name);
        out.append(Format(",\"cat\":\"zone\",\"ph\":\"B\",\"pid\":{},\"tid\":{},\"ts\":{:.3f},\"args\":{{\"file\":", pid, tid, timestampUs));
        AppendJsonString(out, site.file);
        out.append(Format(",\"line\":{}}}}}", site.line));
    }

    /// The `E` event of a zone; the inclusive allocation counts are its args only with ENGINE_PROFILER_ALLOC
    inline void AppendZoneEndEvent(String& out, int pid, uint32_t tid, double timestampUs, [[maybe_unused]] uint64_t allocCount, [[maybe_unused]] uint64_t allocBytes) {
        out.append(Format("{{\"ph\":\"E\",\"pid\":{},\"tid\":{},\"ts\":{:.3f}", pid, tid, timestampUs));
        if constexpr (BE_PROFILER_ALLOC != 0) {
            out.append(Format(",\"args\":{{\"allocs\":{},\"bytes\":{}}}", allocCount, allocBytes));
        }
        out.append("}");
    }

}  // namespace BECore
