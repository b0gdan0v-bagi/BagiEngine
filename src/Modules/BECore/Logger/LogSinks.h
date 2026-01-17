#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Utils/EnumUtils.h>
#include <EASTL/vector.h>

#include <fstream>
#include <mutex>

namespace BECore {

    /**
     * @brief Enum defining available log sink types
     */
    CORE_ENUM(LogSinkType, uint8_t, Console, File)

    /**
     * @brief Log sink that outputs to console (stdout/stderr)
     * 
     * Supports colored output using ANSI escape codes.
     * Error and Fatal messages go to stderr, others to stdout.
     */
    class ConsoleSink : public ILogSink {
    public:
        ConsoleSink() = default;
        ~ConsoleSink() override = default;

        /**
         * @brief Initialize the console sink
         */
        void Initialize() override;

        /**
         * @brief Write a log message to console
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Write(LogLevel level, const char* message, const char* file, int line) override;

        /**
         * @brief Flush console output
         */
        void Flush() override;

        /**
         * @brief Enable or disable colored output
         * 
         * @param enabled true to enable colors
         */
        void SetColorEnabled(bool enabled) { _colorEnabled = enabled; }

        /**
         * @brief Check if colored output is enabled
         * 
         * @return true if colors are enabled
         */
        bool IsColorEnabled() const { return _colorEnabled; }

    private:
        bool _colorEnabled = true;
        bool _initialized = false;
    };

    /**
     * @brief Log sink that outputs to a file
     * 
     * Thread-safe file logging with optional append mode.
     */
    class FileSink : public ILogSink {
    public:
        FileSink() = default;
        ~FileSink() override;

        /**
         * @brief Initialize the file sink
         */
        void Initialize() override;

        /**
         * @brief Check if the file was opened successfully
         * 
         * @return true if file is open and ready for writing
         */
        bool IsOpen() const { return _file.is_open(); }

        /**
         * @brief Write a log message to file
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Write(LogLevel level, const char* message, const char* file, int line) override;

        /**
         * @brief Flush file output
         */
        void Flush() override;

        /**
         * @brief Set the filename for logging
         * @param filename Path to the log file
         */
        void SetFilename(const std::string& filename) { _filename = filename; }

        /**
         * @brief Get the filename
         * @return Path to the log file
         */
        const std::string& GetFilename() const { return _filename; }

        /**
         * @brief Set append mode
         * @param append If true, append to existing file; otherwise truncate
         */
        void SetAppend(bool append) { _append = append; }

        /**
         * @brief Check if append mode is enabled
         * @return true if append mode is enabled
         */
        bool IsAppend() const { return _append; }

    private:
        std::string _filename = "engine.log";
        bool _append = false;
        std::ofstream _file;
        std::mutex _mutex;
        bool _initialized = false;
    };

    /**
     * @brief Manager for all log sinks
     * 
     * Manages log sinks based on XML configuration.
     * Sinks are loaded from config/LoggerConfig.xml
     * and sorted by priority.
     * 
     * @note Access via CoreManager::GetLoggerManager()
     * 
     * @example
     * // Access specific sink
     * auto* sink = CoreManager::GetLoggerManager().GetSink<ConsoleSink>();
     */
    class LoggerManager {
    public:
        LoggerManager() = default;
        ~LoggerManager() = default;

        /**
         * @brief Initialize sinks from configuration
         * 
         * Loads config/LoggerConfig.xml, creates sinks,
         * sorts them by priority, and initializes each one.
         * Safe to call multiple times - subsequent calls are no-ops.
         */
        void Initialize();

        /**
         * @brief Log a message to all sinks
         * 
         * @param level Severity level of the message
         * @param message The formatted log message
         * @param file Source file (can be nullptr)
         * @param line Source line (0 if not available)
         */
        void Log(LogLevel level, const char* message, const char* file, int line);

        /**
         * @brief Flush all sinks
         */
        void Flush();

        /**
         * @brief Get sink by type
         * 
         * @tparam T Sink type to find
         * @return Pointer to sink or nullptr if not found
         */
        template<typename T>
        T* GetSink() {
            for (auto& sink : _sinks) {
                if (auto* typed = dynamic_cast<T*>(sink.Get())) {
                    return typed;
                }
            }
            return nullptr;
        }

        /**
         * @brief Get all sinks
         * @return Reference to sinks vector
         */
        const eastl::vector<IntrusivePtr<ILogSink>>& GetSinks() const { return _sinks; }

    private:
        /**
         * @brief Create sink instance by type
         * @param type Sink type from enum
         * @return Pointer to sink instance or nullptr if unknown type
         */
        static IntrusivePtr<ILogSink> CreateSinkByType(LogSinkType type);

        /**
         * @brief Sort sinks by priority (lower first)
         */
        void SortSinksByPriority();

        eastl::vector<IntrusivePtr<ILogSink>> _sinks;
        bool _initialized = false;
    };

}  // namespace BECore
