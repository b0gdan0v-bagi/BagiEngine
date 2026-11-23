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

        // Not movable: a connection is bound to the address that subscribed, so a moved holder would keep calling the object it left, after
        // that object died too
        SubscriptionHolder(const SubscriptionHolder&) = delete;
        SubscriptionHolder& operator=(const SubscriptionHolder&) = delete;
        SubscriptionHolder(SubscriptionHolder&&) = delete;
        SubscriptionHolder& operator=(SubscriptionHolder&&) = delete;

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
        /// Connections held inside the object, so the first subscriptions of a component, widget or awaiter allocate nothing. Every holder
        /// carries this much, whether it subscribes or not: a holder with more connections overflows to the heap once
        static constexpr size_t kInlineConnections = 2;

        SmallVector<EventConnection, kInlineConnections> _connections;
    };

}  // namespace BECore
