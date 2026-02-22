#pragma once

#include <BECore/Assert/IAssertHandler.h>

namespace BECore {

    /**
     * @brief Handler that triggers debug break on assertion failures
     *
     * Subscribes to AssertEvent and calls platform-specific debug break
     * when an assertion fails. This allows debuggers to stop at the
     * exact point of failure.
     */
    class DebugBreakHandler : public IAssertHandler {
        BE_CLASS(DebugBreakHandler)

    public:
        DebugBreakHandler() = default;
        ~DebugBreakHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;

        void SetEnabled(bool enabled) { _enabled = enabled; }

        bool IsEnabled() const { return _enabled; }

    private:
        BE_REFLECT_FIELD bool _enabled = true;
        bool _initialized = false;
    };

    class LogHandler : public IAssertHandler {
        BE_CLASS(LogHandler)

    public:
        LogHandler() = default;
        ~LogHandler() override = default;

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
        BE_CLASS(StackTraceHandler)

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

        void SortHandlersByPriority();

        eastl::vector<IntrusivePtrAtomic<IAssertHandler>> _handlers;
        bool _initialized = false;
    };

}  // namespace BECore
