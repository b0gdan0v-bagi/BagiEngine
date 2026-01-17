#pragma once

#include <BECore/Assert/IAssertHandler.h>
#include <BECore/Utils/EnumUtils.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief Enum defining available assert handler types
     */
    CORE_ENUM(AssertHandlerType, uint8_t, DebugBreak, Log)

    /**
     * @brief Handler that triggers debug break on assertion failures
     * 
     * Subscribes to AssertEvent and calls platform-specific debug break
     * when an assertion fails. This allows debuggers to stop at the
     * exact point of failure.
     */
    class DebugBreakHandler : public IAssertHandler {
    public:
        DebugBreakHandler() = default;
        ~DebugBreakHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;
        int GetPriority() const override { return _priority; }

        /**
         * @brief Set handler priority
         * @param priority Priority value (lower = higher priority)
         */
        void SetPriority(int priority) { _priority = priority; }

        /**
         * @brief Enable or disable debug breaks
         * @param enabled true to enable debug breaks
         */
        void SetEnabled(bool enabled) { _enabled = enabled; }

        /**
         * @brief Check if debug breaks are enabled
         * @return true if debug breaks are enabled
         */
        bool IsEnabled() const { return _enabled; }

    private:
        int _priority = 0;
        bool _enabled = true;
        bool _initialized = false;
    };

    /**
     * @brief Handler that logs assertion failures
     * 
     * Subscribes to AssertEvent and logs detailed information
     * about the failure using the Logger system.
     */
    class AssertLogHandler : public IAssertHandler {
    public:
        AssertLogHandler() = default;
        ~AssertLogHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;
        int GetPriority() const override { return _priority; }

        /**
         * @brief Set handler priority
         * @param priority Priority value (lower = higher priority)
         */
        void SetPriority(int priority) { _priority = priority; }

    private:
        int _priority = 0;
        bool _initialized = false;
    };

    /**
     * @brief Manager for all assertion handlers
     * 
     * Manages assert handlers based on XML configuration.
     * Handlers are loaded from config/AssertHandlersConfig.xml
     * and sorted by priority.
     * 
     * @note Access via CoreManager::GetAssertHandlerManager()
     * 
     * @example
     * // Access specific handler
     * auto* handler = CoreManager::GetAssertHandlerManager().GetHandler<DebugBreakHandler>();
     */
    class AssertHandlerManager {
    public:
        AssertHandlerManager() = default;
        ~AssertHandlerManager() = default;

        /**
         * @brief Initialize handlers from configuration
         * 
         * Loads config/AssertHandlersConfig.xml, creates handlers,
         * sorts them by priority, and initializes each one.
         * Safe to call multiple times - subsequent calls are no-ops.
         */
        void Initialize();

        /**
         * @brief Get handler by type
         * 
         * @tparam T Handler type to find
         * @return Pointer to handler or nullptr if not found
         */
        template<typename T>
        T* GetHandler() {
            for (auto& handler : _handlers) {
                if (auto* typed = dynamic_cast<T*>(handler.Get())) {
                    return typed;
                }
            }
            return nullptr;
        }

        /**
         * @brief Get all handlers
         * @return Reference to handlers vector
         */
        const eastl::vector<IntrusivePtr<IAssertHandler>>& GetHandlers() const { return _handlers; }

    private:
        /**
         * @brief Create handler instance by type
         * @param type Handler type from enum
         * @return Pointer to handler instance or nullptr if unknown type
         */
        static IntrusivePtr<IAssertHandler> CreateHandlerByType(AssertHandlerType type);

        /**
         * @brief Sort handlers by priority (lower first)
         */
        void SortHandlersByPriority();

        eastl::vector<IntrusivePtr<IAssertHandler>> _handlers;
        bool _initialized = false;
    };

}  // namespace BECore
