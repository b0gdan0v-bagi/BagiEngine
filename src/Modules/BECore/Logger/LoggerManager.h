#pragma once

#include <BECore/Logger/ILogSink.h>

namespace BECore {

    /**
     * @brief Manager for all log sinks
     *
     * Manages log sinks based on XML configuration.
     * Sinks are loaded from config/LoggerConfig.xml
     * and sorted by priority.
     *
     * @note Access via CoreManager::GetLoggerManager()
     * @note Created via BECore::MakeFromConfig<LoggerManager>("LoggerConfig")
     *
     * @example
     * // Access specific sink
     * auto* sink = CoreManager::GetLoggerManager().GetSink<ConsoleSink>();
     */
    class LoggerManager : public RefCountedAtomic {
        BE_CLASS(LoggerManager)

    public:
        LoggerManager() = default;
        ~LoggerManager() override = default;

        /**
         * @brief Initialize sinks from configuration
         *
         * Deserializes _sinks from config/LoggerConfig.xml via the reflection system.
         * Sorts them by priority and initializes each one.
         * Safe to call multiple times - subsequent calls are no-ops.
         *
         * Sinks automatically subscribe to LogEvent during initialization.
         */
        void Initialize();

    private:
        /**
         * @brief Get sink by type
         *
         * @tparam T Sink type to find
         * @return Pointer to sink or nullptr if not found
         */
        template <typename T>
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
        const eastl::vector<IntrusivePtrAtomic<ILogSink>>& GetSinks() const {
            return _sinks;
        }

        /**
         * @brief Sort sinks by priority (lower first)
         */
        void SortSinksByPriority();

        BE_REFLECT_FIELD eastl::vector<IntrusivePtrAtomic<ILogSink>> _sinks;
        bool _initialized = false;
    };

}  // namespace BECore
