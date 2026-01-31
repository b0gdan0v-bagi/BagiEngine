#include "FileSink.h"

#include <BECore/Reflection/IDeserializer.h>
#include <BECore/Logger/LogLevel.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <Generated/FileSink.gen.hpp>
#include <chrono>
#include <fmt/chrono.h>
#include <fmt/core.h>

namespace BECore {

    FileSink::~FileSink() {
        if (_file.is_open()) {
            _file.close();
        }
    }

    void FileSink::Initialize() {
        if (_initialized) {
            return;
        }

        Subscribe<LogEvent, &FileSink::OnLogEvent>(this);
        Subscribe<FlushLogsEvent, &FileSink::OnFlushEvent>(this);

        auto mode = std::ios::out;
        if (_append) {
            mode |= std::ios::app;
        }
        _file.open(_filename.c_str(), mode);

        _initialized = true;
    }

    void FileSink::Configure(IDeserializer& deserializer) {
        Deserialize(deserializer);
    }

    void FileSink::OnLogEvent(const LogEvent& event) {
        Write(event.level, event.message);
    }

    void FileSink::OnFlushEvent() {
        std::scoped_lock lock(_mutex);
        if (_file.is_open()) {
            _file.flush();
        }
    }

    void FileSink::Write(LogLevel level, eastl::string_view message) {
        if (!ShouldLog(level) || !_file.is_open()) {
            return;
        }

        // Get current time
        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Thread-safe localtime conversion
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time);
#else
        localtime_r(&time, &tm_buf);
#endif

        std::scoped_lock lock(_mutex);

        // Format and output (no colors in file)
        _file << fmt::format("[{}] [{:%Y-%m-%d %H:%M:%S}.{:03d}] {}\n", level, tm_buf, ms.count(), message.data());
    }

    void FileSink::SetFilename(eastl::string_view filename) {
        _filename = filename;
    }

}  // namespace BECore
