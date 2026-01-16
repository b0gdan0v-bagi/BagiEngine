#pragma once

#include <Core/Assert/AssertEvent.h>
#include <Core/Utils/Singleton.h>

namespace Core {

    /**
     * @brief Handler that triggers debug break on assertion failures
     * 
     * Subscribes to AssertEvent and calls platform-specific debug break
     * when an assertion fails. This allows debuggers to stop at the
     * exact point of failure.
     * 
     * @example
     * // Initialize at startup
     * DebugBreakHandler::GetInstance().Initialize();
     */
    class DebugBreakHandler : public Singleton<DebugBreakHandler> {
        friend class Singleton<DebugBreakHandler>;

    public:
        ~DebugBreakHandler() override = default;

        /**
         * @brief Initialize and subscribe to assert events
         * 
         * Call this during engine initialization to enable
         * debug break on assertion failures.
         */
        void Initialize();

        /**
         * @brief Enable or disable debug breaks
         * 
         * When disabled, assertion failures are still logged
         * but won't trigger a debug break.
         * 
         * @param enabled true to enable debug breaks
         */
        void SetEnabled(bool enabled) { _enabled = enabled; }

        /**
         * @brief Check if debug breaks are enabled
         * 
         * @return true if debug breaks are enabled
         */
        bool IsEnabled() const { return _enabled; }

    private:
        DebugBreakHandler() = default;

        void OnAssert(const AssertEvent& event);

        bool _enabled = true;
        bool _initialized = false;
    };

    /**
     * @brief Handler that logs assertion failures
     * 
     * Subscribes to AssertEvent and logs detailed information
     * about the failure using the Logger system.
     * 
     * @example
     * // Initialize at startup (after Logger)
     * LogHandler::GetInstance().Initialize();
     */
    class LogHandler : public Singleton<LogHandler> {
        friend class Singleton<LogHandler>;

    public:
        ~LogHandler() override = default;

        /**
         * @brief Initialize and subscribe to assert events
         * 
         * Call this during engine initialization after the
         * Logger has been set up.
         */
        void Initialize();

    private:
        LogHandler() = default;

        void OnAssert(const AssertEvent& event);

        bool _initialized = false;
    };

    /**
     * @brief Initialize all default assert handlers
     * 
     * Convenience function to set up both DebugBreakHandler
     * and LogHandler. Call this during engine startup.
     * 
     * @example
     * // In main() or engine initialization
     * Core::InitializeAssertHandlers();
     */
    void InitializeAssertHandlers();

}  // namespace Core
