#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/Logger/LogSinkType.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <EASTL/vector.h>

namespace BECore {

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
