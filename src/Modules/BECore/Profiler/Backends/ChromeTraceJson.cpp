#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <cerrno>
#include <cstdio>

namespace BECore {

    std::expected<void, String> WriteTextFile(StringView path, const String& text) {
        const String fileName(path.data(), path.size());
        std::FILE* file = std::fopen(fileName.c_str(), "wb");
        if (file == nullptr) {
            return std::unexpected(Format("cannot write '{}': {}", fileName, std::strerror(errno)));
        }
        const size_t written = std::fwrite(text.data(), 1, text.size(), file);
        const bool closed = std::fclose(file) == 0;
        if (written != text.size() || !closed) {
            return std::unexpected(Format("cannot write '{}': the disk refused part of it", fileName));
        }
        return {};
    }

}  // namespace BECore
