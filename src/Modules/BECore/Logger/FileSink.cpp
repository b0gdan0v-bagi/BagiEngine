#include "FileSink.h"

#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>

namespace BECore {

    FileSink::FileSink(const std::string& filename, bool append) {
        auto mode = std::ios::out;
        if (append) {
            mode |= std::ios::app;
        }
        _file.open(filename, mode);
    }

    FileSink::~FileSink() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    void FileSink::Write(LogLevel level, const char* message, const char* file, int line) {
        if (!ShouldLog(level) || !_file.is_open()) {
            return;
        }

        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::lock_guard<std::mutex> lock(_mutex);

        // Format and output (no colors in file)
        if (file && line > 0) {
            _file << fmt::format(
                "[{:5}] [{:%Y-%m-%d %H:%M:%S}.{:03d}] {} ({}:{})\n",
                LogLevelToString(level),
                fmt::localtime(time), ms.count(),
                message, file, line);
        } else {
            _file << fmt::format(
                "[{:5}] [{:%Y-%m-%d %H:%M:%S}.{:03d}] {}\n",
                LogLevelToString(level),
                fmt::localtime(time), ms.count(),
                message);
        }
    }

    void FileSink::Flush() {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_file.is_open()) {
            _file.flush();
        }
    }

}  // namespace BECore
