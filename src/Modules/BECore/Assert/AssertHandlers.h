#pragma once

#include <BECore/Assert/IAssertHandler.h>
#include <BECore/Utils/EnumUtils.h>
#include <EASTL/vector.h>

namespace BECore {

    /**
     * @brief Enum defining available assert handler types
     */
    CORE_ENUM(AssertHandlerType, uint8_t, DebugBreak, Log, StackTrace)

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

        void SetEnabled(bool enabled) { _enabled = enabled; }

        bool IsEnabled() const { return _enabled; }

    private:
        bool _enabled = true;
        bool _initialized = false;
    };

    class AssertLogHandler : public IAssertHandler {
    public:
        AssertLogHandler() = default;
        ~AssertLogHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;

    private:
        bool _initialized = false;
    };

    /**
     * @brief Handler that captures and prints stack traces on assertion failures
     * 
     * Subscribes to AssertEvent and outputs a formatted stack trace to stderr
     * when an assertion fails. This helps identify the call chain leading to
     * the failure.
     */
    class StackTraceHandler : public IAssertHandler {
    public:
        StackTraceHandler() = default;
        ~StackTraceHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;

    private:
        bool _initialized = false;
    };

    class AssertHandlerManager {
    public:
        AssertHandlerManager() = default;
        ~AssertHandlerManager() = default;


        void Initialize();

        template<typename T>
        T* GetHandler() {
            for (auto& handler : _handlers) {
                if (auto* typed = dynamic_cast<T*>(handler.Get())) {
                    return typed;
                }
            }
            return nullptr;
        }

        const eastl::vector<IntrusivePtrAtomic<IAssertHandler>>& GetHandlers() const {
            return _handlers;
        }

    private:

        static IntrusivePtrAtomic<IAssertHandler> CreateHandlerByType(AssertHandlerType type);

        void SortHandlersByPriority();

        eastl::vector<IntrusivePtrAtomic<IAssertHandler>> _handlers;
        bool _initialized = false;
    };

}  // namespace BECore
