#include <BECore/Profiler/Backends/ChromeTraceJson.h>
#include <cerrno>

namespace BECore {

    std::expected<TraceFileWriter, String> TraceFileWriter::Open(StringView path) {
        String fileName(path.data(), path.size());
        std::FILE* file = std::fopen(fileName.c_str(), "wb");
        if (file == nullptr) {
            return std::unexpected(Format("cannot write '{}': {}", fileName, std::strerror(errno)));
        }
        return TraceFileWriter(file, std::move(fileName));
    }

    TraceFileWriter::TraceFileWriter(std::FILE* file, String fileName) : _file(file), _fileName(std::move(fileName)) {}

    TraceFileWriter::TraceFileWriter(TraceFileWriter&& other) noexcept : _file(other._file), _fileName(std::move(other._fileName)), _failed(other._failed) {
        other._file = nullptr;
    }

    TraceFileWriter::~TraceFileWriter() {
        if (_file != nullptr) {
            std::fclose(_file);
        }
    }

    void TraceFileWriter::Write(const String& text) {
        if (_file != nullptr && !_failed && std::fwrite(text.data(), 1, text.size(), _file) != text.size()) {
            _failed = true;
        }
    }

    std::expected<void, String> TraceFileWriter::Close() {
        const bool closed = _file != nullptr && std::fclose(_file) == 0;
        _file = nullptr;
        if (_failed || !closed) {
            return std::unexpected(Format("cannot write '{}': the disk refused part of it", _fileName));
        }
        return {};
    }

    std::expected<void, String> WriteTextFile(StringView path, const String& text) {
        auto file = TraceFileWriter::Open(path);
        if (!file) {
            return std::unexpected(std::move(file.error()));
        }
        file->Write(text);
        return file->Close();
    }

}  // namespace BECore
