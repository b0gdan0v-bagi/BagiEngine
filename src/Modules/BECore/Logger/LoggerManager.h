#pragma once

#include <BECore/Logger/ILogSink.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <BECore/RefCounted/RefCounted.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Manager for all log sinks
     *
     * Manages log sinks based on XML configuration.
     * Sinks are loaded from config/LoggerConfig.xml
     * and sorted by priority: the sink with the lowest number gets each line first.
     *
     * @note Access via CoreManager::GetLoggerManager()
     * @note Created via BECore::MakeFromConfig<LoggerManager>("LoggerConfig")
     */
    class LoggerManager : public RefCountedAtomic {
        BE_CLASS(LoggerManager)

    public:
        LoggerManager() = default;
        ~LoggerManager() override = default;

        /**
         * @brief Initialize sinks from configuration
         *
         * Safe to call multiple times - subsequent calls are no-ops.
         *
         * Sinks automatically subscribe to LogEvent during initialization.
         */
        void Initialize();

        /**
         * @brief Get sink by type
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

        /// Sorted ascending by priority (equal ones in config order) once Initialize() has run
        const Vector<IntrusivePtr<ILogSink>>& GetSinks() const {
            return _sinks;
        }

    private:
        void SortSinksByPriority();

        BE_REFLECT_FIELD Vector<IntrusivePtr<ILogSink>> _sinks;
        bool _initialized = false;
    };

}  // namespace BECore
