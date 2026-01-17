#pragma once

#include <BECore/Logger/LogLevel.h>
#include <BECore/GameManager/CoreManager.h>

#include <fmt/core.h>

namespace BECore {

    /**
     * @brief Helper class for formatted logging
     * 
     * Provides static methods for logging through CoreManager's LoggerManager.
     * Use the LOG_* macros for automatic file/line info.
     * 
     * @example
     * LOG_INFO("Game started");
     * LOG_ERROR("Failed to load: {}", filename);
     */
    class Logger {
    public:
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
        static void Log(LogLevel level, const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            std::string message = fmt::format(format, std::forward<Args>(args)...);
            CoreManager::GetLoggerManager().Log(level, message.c_str(), file, line);
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
        static void Log(LogLevel level, fmt::format_string<Args...> format, Args&&... args) {
            std::string message = fmt::format(format, std::forward<Args>(args)...);
            CoreManager::GetLoggerManager().Log(level, message.c_str(), nullptr, 0);
        }

        /**
         * @brief Log a raw message without formatting
         * 
         * @param level Log level
         * @param message Message string
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        static void LogRaw(LogLevel level, const char* message, const char* file = nullptr, int line = 0) {
            CoreManager::GetLoggerManager().Log(level, message, file, line);
        }

        // Convenience methods with location info
        template<typename... Args>
        static void Debug(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Debug, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Info(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Info, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Warning(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Warning, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Error(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Error, file, line, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Fatal(const char* file, int line, fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Fatal, file, line, format, std::forward<Args>(args)...);
        }

        // Convenience methods without location info
        template<typename... Args>
        static void Debug(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Debug, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Info(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Info, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Warning(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Warning, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Error(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Error, format, std::forward<Args>(args)...);
        }

        template<typename... Args>
        static void Fatal(fmt::format_string<Args...> format, Args&&... args) {
            Log(LogLevel::Fatal, format, std::forward<Args>(args)...);
        }

        /**
         * @brief Flush all sinks
         */
        static void Flush() {
            CoreManager::GetLoggerManager().Flush();
        }
    };

}  // namespace BECore

// Convenience macros with automatic file/line info
#define LOG_DEBUG(...)   ::BECore::Logger::Debug(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)    ::BECore::Logger::Info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARNING(...) ::BECore::Logger::Warning(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...)   ::BECore::Logger::Error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...)   ::BECore::Logger::Fatal(__FILE__, __LINE__, __VA_ARGS__)
