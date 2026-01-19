#include "FileSink.h"

#include <BECore/Config/XmlNode.h>
#include <BECore/Logger/LogLevel.h>

#include <EASTL/string_view.h>
#include <EASTL/string.h>
#include <fmt/core.h>
#include <fmt/chrono.h>
#include <chrono>

#include <Generated/FileSink.gen.hpp>

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

        LogEvent::Subscribe<&FileSink::OnLogEvent>(this);
        FlushLogsEvent::Subscribe<&FileSink::OnFlushEvent>(this);

        auto mode = std::ios::out;
        if (_append) {
            mode |= std::ios::app;
        }
        _file.open(_filename.c_str(), mode);

        _initialized = true;
    }

    void FileSink::Configure(const XmlNode& node) {
        auto filename = node.ParseAttribute<eastl::string_view>("filename");
        if (filename.has_value()) {
            SetFilename(*filename);
        }

        auto append = node.ParseAttribute<bool>("append");
        if (append.has_value()) {
            SetAppend(*append);
        }
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
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::lock_guard<std::mutex> lock(_mutex);

        // Format and output (no colors in file)
        _file << fmt::format(
            "[{}] [{:%Y-%m-%d %H:%M:%S}.{:03d}] {}\n",
            level,
            fmt::localtime(time), ms.count(),
            eastl::string(message.data(), message.size()));
    }

    void FileSink::SetFilename(eastl::string_view filename) {
        _filename = filename;
    }

}  // namespace BECore
