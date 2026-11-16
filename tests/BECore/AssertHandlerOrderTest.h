#pragma once

#include <BECore/Assert/IAssertHandler.h>

namespace BECore::Tests {

    /// A handler that writes its `id` to a static list when an assert reaches it, so a test reads the order the handlers ran in.
    /// `flushLog` makes it deliver the pending log lines first: the shipped LogHandler is behind it exactly when it ran before.
    class OrderHandler : public IAssertHandler {
        BE_CLASS(OrderHandler)

    public:
        OrderHandler() = default;
        ~OrderHandler() override = default;

        void Initialize() override;
        void OnAssert(const AssertEvent& event) override;

        BE_REFLECT_FIELD int32_t id = 0;
        BE_REFLECT_FIELD bool flushLog = false;

        static inline Vector<int32_t> order;
        /// Called after a flushing handler delivered the log lines
        static inline void (*afterFlush)() = nullptr;
    };

}  // namespace BECore::Tests
