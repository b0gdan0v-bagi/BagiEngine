#pragma once

namespace BECore {

    /// Appends @p text to @p out as a JSON string literal with its quotes (nullptr is an empty string): the one escaper of every Chrome trace the profiler writes
    inline void AppendJsonString(eastl::string& out, const char* text) {
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

}  // namespace BECore
