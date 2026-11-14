#pragma once

#include "ManualClock.h"
#include "TestRandom.h"

#include <Games/SeatedGameRegistry.h>
#include <RoomClient/RoomTransport.h>
#include <RoomServer/DemoCardGame.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomLimits.h>
#include <deque>
#include <memory>
#include <vector>

namespace BECore::Tests {

    /// One connection between a client and the hub of a HubWorld
    struct HubLink {
        ConnectionId id = 0;
        /// The hub knows the connection and nobody has closed it
        bool open = false;
        /// Nothing passes in either direction, and nobody is told: a cable pulled, a phone out of range
        bool blackhole = false;
        /// What the client polls
        std::deque<RoomTransportEvent> toClient;
        bool closedQueued = false;

        void QueueText(StringView text) {
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Text;
            event.text = String(text);
            toClient.push_back(std::move(event));
        }

        /// The one Closed of the connection; a second call does nothing
        void QueueClosed(uint16_t code, StringView reason) {
            if (closedQueued) {
                return;
            }
            closedQueued = true;
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Closed;
            event.closeCode = code;
            event.reason = String(reason);
            toClient.push_back(std::move(event));
        }
    };

    /// The connections one client made, in order (a reconnect makes a new one)
    struct HubClientLinks {
        std::vector<SharedPtr<HubLink>> links;

        [[nodiscard]] HubLink& Last() {
            return *links.back();
        }
    };

    /**
     * @brief A RoomHub with the demo games (`cards`, `cards-leaky`), a manual clock, and in-memory transports for RoomClients.
     *
     * No socket and no thread: a transport call into the hub is a direct call, and what the hub sends is queued for the client's
     * next PollEvent(). The test moves the clock with Advance(), which also ticks the hub (computers move, the grace runs out).
     * A new connection is refused while SetRefusing(true) (the server is down). The close codes follow RoomWebSocketServer.
     */
    class HubWorld {
    public:
        explicit HubWorld(RoomHubSettings settings, RandomFn random = MakeSequence(11)) : _hub(MakeRegistry(), std::move(settings), std::move(random), MakeSink()) {}

        /// A zero computer delay and the usual grace and timeouts
        static RoomHubSettings MakeSettings(std::chrono::milliseconds computerDelay = std::chrono::milliseconds(0)) {
            RoomHubSettings settings;
            settings.timings = RoomTimings{RoomLimits::kReconnectGrace, RoomLimits::kAbandonedTimeout, computerDelay};
            return settings;
        }

        [[nodiscard]] RoomHub& Hub() {
            return _hub;
        }

        [[nodiscard]] ManualClock& Clock() {
            return _clock;
        }

        [[nodiscard]] RoomTime Now() const {
            return _clock.Now();
        }

        /// Moves the clock and ticks the hub
        void Advance(std::chrono::milliseconds step) {
            _clock.Advance(step);
            _hub.Tick(_clock.Now());
        }

        /// The server is down: a new connection is refused (the transport's Closed, 1006)
        void SetRefusing(bool refusing) {
            _refusing = refusing;
        }

        /// The network breaks the connection: the hub is told, the client sees a Closed (1006)
        void Sever(HubLink& link) {
            if (!link.open) {
                return;
            }
            link.open = false;
            _hub.OnDisconnected(link.id, _clock.Now());
            link.blackhole = false;
            link.QueueClosed(kRoomTransportAbnormalClosure, "connection lost");
        }

        /// The factory of one client's transports; their links are appended to @p links
        [[nodiscard]] RoomTransportFactory Factory(HubClientLinks& links);

        /// The transport calls these
        void Connect(const SharedPtr<HubLink>& link);
        void Deliver(HubLink& link, StringView text);
        void Disconnect(HubLink& link, uint16_t code, StringView reason);

    private:
        SeatedGameRegistry& MakeRegistry() {
            _registry.Register(SeatedGameInfo{"cards", DemoCardGame::kSeatCount}, [] { return MakeUnique<DemoCardGame>(); });
            _registry.Register(SeatedGameInfo{"cards-leaky", DemoCardGame::kSeatCount}, [] { return MakeUnique<LeakyDemoCardGame>(); });
            return _registry;
        }

        HubSink MakeSink() {
            HubSink sink;
            sink.send = [this](ConnectionId id, String text) {
                if (const auto found = _byId.find(id); found != _byId.end() && !found->second->blackhole) {
                    found->second->QueueText(text);
                }
            };
            sink.close = [this](ConnectionId id, String reason) {
                const auto found = _byId.find(id);
                if (found == _byId.end()) {
                    return;
                }
                found->second->open = false;
                if (!found->second->blackhole) {
                    found->second->QueueClosed(GetCloseCode(reason), reason);
                }
            };
            return sink;
        }

        /// What RoomWebSocketServer tells the client when the hub ends its connection
        static uint16_t GetCloseCode(const String& reason) {
            constexpr uint16_t kPolicyViolation = 1008;
            constexpr uint16_t kTryAgainLater = 1013;
            if (reason == "rate_limited" || reason == "too_many_failed_joins") {
                return kPolicyViolation;
            }
            return reason == "server_full" ? kTryAgainLater : kRoomTransportNormalClosure;
        }

        // The registry is destroyed after the hub, which holds a reference to it
        SeatedGameRegistry _registry;
        ManualClock _clock;
        RoomHub _hub;
        Map<ConnectionId, SharedPtr<HubLink>> _byId;
        ConnectionId _nextId = 1;
        bool _refusing = false;
    };

    /// An IRoomTransport that talks to a HubWorld
    class HubTransport final : public IRoomTransport {
    public:
        HubTransport(HubWorld& world, SharedPtr<HubLink> link) : _world(world), _link(std::move(link)) {}

        ~HubTransport() override {
            // Letting go of a connection ends it, as closing the socket does
            if (_link->open) {
                _world.Disconnect(*_link, kRoomTransportAbnormalClosure, "dropped");
            }
        }

        std::expected<void, String> Open(StringView /*url*/) override {
            _world.Connect(_link);
            return {};
        }

        bool SendText(StringView text) override {
            if (!_link->open) {
                return false;
            }
            _world.Deliver(*_link, text);
            return true;
        }

        void Close(uint16_t code, StringView reason) override {
            _world.Disconnect(*_link, code, reason);
        }

        void Abort() override {
            _world.Disconnect(*_link, kRoomTransportAbnormalClosure, "aborted");
        }

        bool PollEvent(RoomTransportEvent& event) override {
            if (_link->toClient.empty()) {
                return false;
            }
            event = std::move(_link->toClient.front());
            _link->toClient.pop_front();
            return true;
        }

    private:
        HubWorld& _world;
        SharedPtr<HubLink> _link;
    };

    inline RoomTransportFactory HubWorld::Factory(HubClientLinks& links) {
        return [this, &links]() -> UniquePtr<IRoomTransport> {
            auto link = MakeShared<HubLink>();
            links.links.push_back(link);
            return MakeUnique<HubTransport>(*this, link);
        };
    }

    inline void HubWorld::Connect(const SharedPtr<HubLink>& link) {
        if (_refusing) {
            link->QueueClosed(kRoomTransportAbnormalClosure, "connection refused");
            return;
        }
        link->id = _nextId++;
        link->open = true;
        _byId[link->id] = link;
        _hub.OnConnected(link->id, _clock.Now());
        RoomTransportEvent opened;
        opened.kind = RoomTransportEventKind::Opened;
        link->toClient.push_back(std::move(opened));
    }

    inline void HubWorld::Deliver(HubLink& link, StringView text) {
        if (link.blackhole) {
            return;
        }
        _hub.OnMessage(link.id, text, _clock.Now());
    }

    inline void HubWorld::Disconnect(HubLink& link, uint16_t code, StringView reason) {
        if (link.open) {
            link.open = false;
            _hub.OnDisconnected(link.id, _clock.Now());
        }
        // A connection that never opened has nothing else to say, and a closed one says its Closed once
        link.blackhole = false;
        link.QueueClosed(code, reason);
    }

}  // namespace BECore::Tests
