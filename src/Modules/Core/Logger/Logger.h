#pragma once

#include <Core/Utils/Singleton.h>
#include <Core/Logger/LogLevel.h>
#include <Core/Logger/ILogSink.h>

#include <EASTL/vector.h>
#include <EASTL/unique_ptr.h>
#include <fmt/core.h>

namespace Core {

    /**
     * @brief Central logging system for the engine
     * 
     * Singleton that manages multiple log sinks (console, file, etc.)
     * and provides formatted logging methods.
     * 
     * @example
     * // Get logger instance
     * auto& logger = Logger::GetInstance();
     * 
     * // Add sinks
     * logger.AddSink(eastl::make_unique<ConsoleSink>());
     * logger.AddSink(eastl::make_unique<FileSink>("game.log"));
     * 
     * // Log messages
     * logger.Info("Game started");
     * logger.Error("Failed to load: {}", filename);
     */
    class Logger : public Singleton<Logger> {
        friend class Singleton<Logger>;

    public:
        ~Logger() override = default;

        /**
         * @brief Add a log sink
         * 
         * @param sink Sink to add (ownership transferred)
         */
        void AddSink(eastl::unique_ptr<ILogSink> sink);

        /**
         * @brief Remove all sinks
         */
        void ClearSinks();

        /**
         * @brief Get the number of registered sinks
         * 
         * @return Number of sinks
         */
        size_t GetSinkCount() const { return _sinks.size(); }

        /**
         * @brief Set minimum log level for all sinks
         * 
         * @param level Minimum level to log
         */
        void SetMinLevel(LogLevel level);

        /**
         * @brief Flush all sinks
         */
        void Flush();

        /**
         * @brief Log a message at specified level
         * 
         * @tparam Args Format argument types
         * @param level Log level
         * @param file Source file (use __FILE__)
         * @param line Source line (use __LINE__)
         * @param format Format string
         * @param args Format arguments
         */
        template<typename... Args>
        void Log(LogLevel level, const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            std::string message = fmt::format(format, std::forward<Args>(args)...);
            LogImpl(level, message.c_str(), file, line);
        }

        /**
         * @brief Log a message at specified level (no location info)
         * 
         * @tparam Args Format argument types
         * @param level Log level
         * @param format Format string
         * @param args Format arguments
         */
        template<typename... Args>
        void Log(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
            std::string message = fmt::format(format, std::forward<Args>(args)...);
            LogImpl(level, message.c_str(), nullptr, 0);
        }

        /**
         * @brief Log a raw message without formatting
         * 
         * @param level Log level
         * @param message Message string
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void LogRaw(LogLevel level, const char* message, const char* file = nullptr, int line = 0) {
            LogImpl(level, message, file, line);
        }

        // Convenience methods with location info
        template<typename... Args>
        void Debug(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Debug, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Info(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Info, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Warning(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Warning, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Error(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Error, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Fatal(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Fatal, file, line, format, std::forward<Args>(args)...);
        }

        // Convenience methods without location info
        template<typename... Args>
        void Debug(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Debug, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Info(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Info, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Warning(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Warning, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Error(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Error, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        void Fatal(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Fatal, format, std::forward<Args>(args)...);
        }

    private:
        Logger() = default;

        void LogImpl(LogLevel level, const char* message, const char* file, int line);

        eastl::vector<eastl::unique_ptr<ILogSink>> _sinks;
    };

}  // namespace Core

// Convenience macros with automatic file/line info
#define LOG_DEBUG(...)   ::Core::Logger::GetInstance().Debug(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)    ::Core::Logger::GetInstance().Info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) ::Core::Logger::GetInstance().Warning(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)   ::Core::Logger::GetInstance().Error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)   ::Core::Logger::GetInstance().Fatal(__FILE__, __LINE__, __VA_ARGS__)
