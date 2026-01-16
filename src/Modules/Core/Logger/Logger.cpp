#include "Logger.h"

namespace Core {

    void Logger::AddSink(eastl::unique_ptr<ILogSink> sink) {
        if (sink) {
            _sinks.push_back(std::move(sink));
        }
    }

    void Logger::ClearSinks() {
        _sinks.clear();
    }

    void Logger::SetMinLevel(LogLevel level) {
        for (auto& sink : _sinks) {
            sink->SetMinLevel(level);
        }
    }

    void Logger::Flush() {
        for (auto& sink : _sinks) {
            sink->Flush();
        }
    }

    void Logger::LogImpl(LogLevel level, const char* message, const char* file, int line) {
        for (auto& sink : _sinks) {
            sink->Write(level, message, file, line);
        }

        // Auto-flush on fatal errors
        if (level == LogLevel::Fatal) {
            Flush();
        }
    }

}  // namespace Core
