#pragma once

#include <RemoteControl/Socket.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <Rooms/RoomLimits.h>

namespace BECore {

    /// Everything a RoomWebSocketServer is configured with. The defaults are the limits of the room server (RoomServer/CLAUDE.md);
    /// a test overrides what it needs, and a server executable fills the struct from its arguments or its environment
    struct RoomServerSettings {
        /// 0 picks a free port (RoomWebSocketServer::GetPort() tells which)
        uint16_t port = 0;

        /// The loopback or every address. A public server binds every address and sets the policy below to a public one
        SocketBind bind = SocketBind::Loopback;

        /// Who may open a connection: the loopback checks, or an origin allow-list (`RemoteControl/CLAUDE.md`, "Security")
        HandshakePolicy policy;

        /// Open sockets, handshakes included. The hub limits the connections it knows (`hub.maxConnections`, answered with `server_full`) and
        /// this is above it, so a connection still in its handshake has room and the hub's answer comes first
        size_t maxConnections = RoomLimits::kMaxConnections + 64;

        /// Open sockets of one client address; 0 turns it off (behind a proxy every client has the proxy's address)
        size_t maxConnectionsPerAddress = 32;

        /// What one connection may hold in its send queue before it is dropped: the messages of a room are small
        size_t maxOutboxBytes = 1024 * 1024;

        /// The limits of the hub (a message over `hub.maxMessageBytes` is closed with 1009 by the transport, before it is buffered)
        RoomHubSettings hub;

        /// How often the hub thread ticks when nothing arrives (real time)
        std::chrono::milliseconds tickInterval{50};

        /// What the hub is told the time is. Empty: the real clock (`steady_clock`). A test moves a manual clock instead of sleeping
        std::function<RoomTime()> clock;

        /// Source of room codes, tokens and seeds. Empty: MakeSecureRandom()
        RandomFn random;
    };

}  // namespace BECore
