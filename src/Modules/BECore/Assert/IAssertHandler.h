#pragma once

#include <BECore/Assert/AssertEvent.h>
#include <BECore/RefCounted/RefCounted.h>
#include <Events/SubscriptionHolder.h>

namespace BECore {

    /**
     * @brief Interface for assertion event handlers
     *
     * Base interface for all assertion handlers. Implement this
     * interface to create custom handlers for assertion failures.
     *
     * Handlers are managed by AssertHandlerManager and sorted by priority.
     * Lower priority values are processed first.
     *
     * @example
     * class MyHandler : public IAssertHandler {
     * public:
     *     void Initialize() override {
     *         AssertEvent::Subscribe<&MyHandler::OnAssert>(this);
     *     }
     *
     *     void OnAssert(const AssertEvent& event) override {
     *         // Handle assertion
     *     }
     * };
     */
    class IAssertHandler : public RefCounted, public SubscriptionHolder {
    public:
        ~IAssertHandler() override = default;

        /**
         * @brief Initialize the handler and subscribe to events
         *
         * Called during engine initialization. Should subscribe
         * to AssertEvent and set up any necessary state.
         */
        virtual void Initialize() = 0;

        /**
         * @brief Handle an assertion event
         *
         * Called when an assertion fails. Implement to define
         * custom behavior for assertion handling.
         *
         * @param event The assertion event with details about the failure
         */
        virtual void OnAssert(const AssertEvent& event) = 0;

        /**
         * @brief Get handler priority for sorting
         *
         * Lower values are processed first. Use this to control
         * the order in which handlers receive events.
         *
         * @return Priority value (lower = higher priority)
         */
        virtual int GetPriority() const {
            return _priority;
        }

        /**
         * @brief Set handler priority
         * @param priority Priority value (lower = higher priority)
         */
        void SetPriority(int priority) {
            _priority = priority;
        }

    protected:
        IAssertHandler() = default;

        int _priority = 0;
    };

}  // namespace BECore
