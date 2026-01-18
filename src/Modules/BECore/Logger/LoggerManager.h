#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <EASTL/vector.h>

// Forward declaration - ILogSinkType is generated from BE_CLASS(ILogSink, FACTORY_BASE)
enum class LogSinkType : uint8_t;

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
         * 
         * Sinks automatically subscribe to LogEvent during initialization.
         * LoggerManager subscribes to FlushEvent to handle flush requests.
         */
        void Initialize();

    private:
        /**
         * @brief Handle FlushEvent
         */
        void OnFlushEvent();

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
         * @param type Sink type from generated enum (ILogSinkType)
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
