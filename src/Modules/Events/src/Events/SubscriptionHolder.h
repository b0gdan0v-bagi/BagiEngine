#pragma once

#include <EASTL/vector.h>
#include <entt/entt.hpp>

namespace BECore {

    /**
     * @brief RAII holder for event subscriptions using entt::scoped_connection
     * 
     * This class manages event subscriptions and automatically unsubscribes
     * when the object is destroyed. Inherit from this class to get automatic
     * subscription management.
     * 
     * @example
     * class MyWidget : public SubscriptionHolder {
     * public:
     *     void Initialize() {
     *         Subscribe<NewFrameEvent, &MyWidget::OnNewFrame>(this);
     *         Subscribe<RenderEvent, &MyWidget::OnRender>(this);
     *     }
     * 
     *     void OnNewFrame(const NewFrameEvent& e) { ... }
     *     void OnRender(const RenderEvent& e) { ... }
     * };
     */
    class SubscriptionHolder {
    public:
        SubscriptionHolder() = default;

        virtual ~SubscriptionHolder() = default;

        // Non-copyable, movable
        SubscriptionHolder(const SubscriptionHolder&) = delete;
        SubscriptionHolder& operator=(const SubscriptionHolder&) = delete;
        SubscriptionHolder(SubscriptionHolder&&) = default;
        SubscriptionHolder& operator=(SubscriptionHolder&&) = default;

    protected:
        /**
         * @brief Subscribe to an event and store the connection for auto-cleanup
         * 
         * @tparam Event Event type (must have Subscribe method)
         * @tparam Handler Member function pointer to handler
         * @tparam T Type of the instance
         * @param instance Pointer to the instance containing the handler
         * 
         * @example
         * Subscribe<NewFrameEvent, &MyWidget::OnNewFrame>(this);
         */
        template <typename Event, auto Handler, typename T>
        void Subscribe(T* instance) {
            _connections.push_back(Event::template Subscribe<Handler>(instance, {}));
        }

        /**
         * @brief Manually unsubscribe all held connections
         * 
         * This is automatically called in the destructor, but can be
         * called manually if needed.
         */
        void UnsubscribeAll() {
            _connections.clear();  // scoped_connection destructor handles disconnect
        }

    private:
        eastl::vector<entt::scoped_connection> _connections;
    };

}  // namespace BECore
