#pragma once

#include <BECore/Types/Types.h>

namespace BECore {

    class EventChannel;

    /// Calls one handler: `instance` is the object the handler was connected for, `event` the published event.
    using EventInvoker = void (*)(void* instance, const void* event);

    /**
     * @brief Ends a subscription when it is destroyed or reset.
     *
     * It disconnects by (instance, invoker), not by a token: if the same pair was connected again by someone else,
     * dropping the older connection removes the one listener that the pair is.
     */
    class EventConnection {
    public:
        EventConnection() = default;
        ~EventConnection() {
            Reset();
        }

        EventConnection(const EventConnection&) = delete;
        EventConnection& operator=(const EventConnection&) = delete;

        EventConnection(EventConnection&& other) noexcept : _channel(other._channel), _instance(other._instance), _invoker(other._invoker) {
            other._channel = nullptr;
        }

        EventConnection& operator=(EventConnection&& other) noexcept {
            if (this != &other) {
                Reset();
                _channel = other._channel;
                _instance = other._instance;
                _invoker = other._invoker;
                other._channel = nullptr;
            }
            return *this;
        }

        /// Disconnects now; an already removed pair is a no-op.
        inline void Reset();

    private:
        friend class EventChannel;

        EventConnection(EventChannel* channel, void* instance, EventInvoker invoker) : _channel(channel), _instance(instance), _invoker(invoker) {}

        EventChannel* _channel = nullptr;
        void* _instance = nullptr;
        EventInvoker _invoker = nullptr;
    };

    /**
     * @brief The listeners of one event type, type-erased: an ordered list of (instance, invoker) pairs.
     *
     * - `Publish` calls the listeners from the last connected to the first, over the count read at its start, so a
     *   listener connected by a handler is not called in that dispatch.
     * - (instance, invoker) is the identity of a listener: connecting a pair again removes the old entry and appends.
     * - A disconnect moves the last entry into the freed slot instead of shifting the tail, so the order of the
     *   others changes; the order of subscribers after a re-subscribe is part of the contract.
     *
     * A disconnect inside `Publish` is tolerated: the entry is marked, skipped if it has not run yet, and the list
     * is compacted (order kept) when the outermost `Publish` ends. A connect appends.
     * One thread only; a channel is never moved or destroyed while a connection to it lives (the channel of an
     * event type is a static that is never freed).
     *
     * Everything is inline: `BECoreModule` emits and subscribes to events but does not link `EventsModule`.
     */
    class EventChannel {
    public:
        EventChannel() = default;
        EventChannel(const EventChannel&) = delete;
        EventChannel& operator=(const EventChannel&) = delete;

        [[nodiscard]] EventConnection Connect(void* instance, EventInvoker invoker) {
            Disconnect(instance, invoker);
            _listeners.push_back({instance, invoker});
            return EventConnection(this, instance, invoker);
        }

        void Disconnect(void* instance, EventInvoker invoker) {
            for (size_t pos = _listeners.size(); pos; --pos) {
                if (const Listener& listener = _listeners[pos - 1]; listener.instance == instance && listener.invoker == invoker) {
                    Remove(pos - 1);
                }
            }
        }

        void DisconnectInstance(void* instance) {
            for (size_t pos = _listeners.size(); pos; --pos) {
                if (const Listener& listener = _listeners[pos - 1]; listener.invoker != nullptr && listener.instance == instance) {
                    Remove(pos - 1);
                }
            }
        }

        void Publish(const void* event) {
            ++_publishDepth;
            for (size_t pos = _listeners.size(); pos; --pos) {
                // A copy: the handler may connect and grow the list.
                const Listener listener = _listeners[pos - 1];
                if (listener.invoker != nullptr) {
                    listener.invoker(listener.instance, event);
                }
            }
            if (--_publishDepth == 0 && _hasRemoved) {
                Compact();
            }
        }

    private:
        struct Listener {
            void* instance = nullptr;
            EventInvoker invoker = nullptr;  // null: removed while a dispatch was running
        };

        void Remove(size_t index) {
            if (_publishDepth > 0) {
                _listeners[index].invoker = nullptr;
                _hasRemoved = true;
                return;
            }
            if (index + 1 != _listeners.size()) {
                _listeners[index] = _listeners.back();
            }
            _listeners.pop_back();
        }

        void Compact() {
            size_t kept = 0;
            for (const Listener& listener : _listeners) {
                if (listener.invoker != nullptr) {
                    _listeners[kept++] = listener;
                }
            }
            _listeners.resize(kept);
            _hasRemoved = false;
        }

        Vector<Listener> _listeners;
        uint32_t _publishDepth = 0;
        bool _hasRemoved = false;
    };

    inline void EventConnection::Reset() {
        if (_channel != nullptr) {
            _channel->Disconnect(_instance, _invoker);
            _channel = nullptr;
        }
    }

}  // namespace BECore
