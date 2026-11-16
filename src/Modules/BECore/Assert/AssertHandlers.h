#pragma once

#include <BECore/Assert/IAssertHandler.h>
#include <BECore/RefCounted/IntrusivePtr.h>

namespace BECore {

    class DebugBreakHandler : public IAssertHandler {
        BE_CLASS(DebugBreakHandler)

    public:
        DebugBreakHandler() = default;
        ~DebugBreakHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;

        void SetEnabled(bool enabled) {
            _enabled = enabled;
        }

        bool IsEnabled() const {
            return _enabled;
        }

    private:
        BE_REFLECT_FIELD bool _enabled = true;
    };

    class LogHandler : public IAssertHandler {
        BE_CLASS(LogHandler)

    public:
        LogHandler() = default;
        ~LogHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;
    };

    class StackTraceHandler : public IAssertHandler {
        BE_CLASS(StackTraceHandler)

    public:
        StackTraceHandler() = default;
        ~StackTraceHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;
    };

    class AssertHandlerManager : public RefCountedAtomic {
        BE_CLASS(AssertHandlerManager)

    public:
        AssertHandlerManager() = default;
        ~AssertHandlerManager() override = default;

        void Initialize();

        template <typename T>
        T* GetHandler() {
            for (auto& handler : _handlers) {
                if (auto* typed = dynamic_cast<T*>(handler.Get())) {
                    return typed;
                }
            }
            return nullptr;
        }

        const Vector<IntrusivePtrAtomic<IAssertHandler>>& GetHandlers() const {
            return _handlers;
        }

    private:
        void SortHandlersByPriority();

        BE_REFLECT_FIELD Vector<IntrusivePtrAtomic<IAssertHandler>> _handlers;
        bool _initialized = false;
    };

}  // namespace BECore
