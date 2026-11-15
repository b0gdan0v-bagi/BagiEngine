#pragma once

#include <BECore/Types/Types.h>
#include <Events/EventChannel.h>

namespace BECore {

    /**
     * @brief RAII holder for event subscriptions using EventConnection
     *
     * The subscriptions end when the holder is destroyed. Inherit from it to subscribe (usage: Events/CLAUDE.md).
     */
    class SubscriptionHolder {
    public:
        SubscriptionHolder() = default;

        virtual ~SubscriptionHolder() = default;

        // A moved holder takes the connections, but they stay bound to the address that subscribed.
        SubscriptionHolder(const SubscriptionHolder&) = delete;
        SubscriptionHolder& operator=(const SubscriptionHolder&) = delete;
        SubscriptionHolder(SubscriptionHolder&&) = default;
        SubscriptionHolder& operator=(SubscriptionHolder&&) = default;

    protected:
        /// Subscribes `instance` and keeps the connection until the destructor or UnsubscribeAll().
        template <typename Event, auto Handler, typename T>
        void Subscribe(T* instance) {
            _connections.push_back(Event::template Subscribe<Handler>(instance, {}));
        }

        /// Drops every held connection now; the destructor does the same.
        void UnsubscribeAll() {
            _connections.clear();  // each connection disconnects in its destructor
        }

    private:
        Vector<EventConnection> _connections;
    };

}  // namespace BECore
