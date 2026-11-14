#pragma once

#include <BECore/Control/ControlTypes.h>
#include <RemoteControl/WebSocketConnection.h>

namespace BECore {

    /// Error codes of WebSocketListener::Start() (and of the servers built on it)
    namespace RemoteControlErrors {
        inline constexpr StringView kListenFailed = "listen_failed";
        inline constexpr StringView kAlreadyRunning = "already_running";
    }  // namespace RemoteControlErrors

    /// The limits and names of one listener. The defaults suit a small public server; the control server overrides them
    struct WebSocketListenerSettings {
        /// The loopback or every address
        SocketBind bind = SocketBind::Loopback;

        /// Who may open a connection (RemoteControl/CLAUDE.md, "Security")
        HandshakePolicy policy;

        /// Open connections, handshakes included; the next client gets a 503
        size_t maxConnections = 64;

        /// The same for one client address; 0 turns the limit off (behind a proxy every client has the proxy's address, unless
        /// policy.trustedProxyHops says whose word to take: then the limit counts the addresses the proxies report, checked once
        /// the handshake is accepted)
        size_t maxConnectionsPerAddress = 0;

        /// A longer message closes the connection with 1009, at the frame header, before it is buffered
        size_t maxMessageBytes = ControlService::kMaxRequestBytes;

        /// What a connection may hold in its send queue before it is dropped
        size_t maxOutboxBytes = 1024 * 1024;

        /// How long a client has to complete the handshake after it connected
        std::chrono::milliseconds handshakeTimeout{5000};

        /// The prefix of the log lines
        String logName = "WebSocket";

        /// The text of the 503 a refused client gets
        String busyMessage = "the server has too many connections";

        /// The names of the threads in the profiler: `<threadName> accept`, `reader` and `writer`, sorted from `threadOrder`
        String threadName = "WebSocket";
        uint32_t threadOrder = 200;
    };

    /**
     * @brief What a listener does with the connections it serves. The functions run on the reader thread of the connection (so
     * two calls for one connection never overlap, and calls for different connections do).
     *
     * `onClosed` runs exactly once for every connection for which `onOpen` ran, when the connection has ended for any reason
     * (the client left, a protocol error, a refusal of `onMessage`, Stop()), after its writer has stopped.
     */
    struct WebSocketHandler {
        using ConnectionPtr = SharedPtr<WebSocketConnection>;

        /// The handshake succeeded and the connection takes frames
        std::function<void(const ConnectionPtr&)> onOpen;

        /// A text message of the client (a ping, a pong, a close, binary data and protocol errors are the listener's). False ends the
        /// connection: queue a close frame first if the client is to hear why
        std::function<bool(const ConnectionPtr&, WebSocketMessage)> onMessage;

        std::function<void(const ConnectionPtr&)> onClosed;
    };

    /**
     * @brief The server half of the WebSocket transport: a listening socket, an accept thread and per connection a reader and a
     * writer thread (WebSocketConnection). It knows nothing about what the messages mean; the WebSocketHandler does.
     *
     * The control API's server and the room server are built on it. Start() and Stop() belong to one thread (the owner's); the
     * handler runs on the reader threads. Stop() ends every connection and joins every thread: no handler function runs after it
     * returns, so it must not be called from a handler function.
     */
    class WebSocketListener {
    public:
        WebSocketListener(WebSocketListenerSettings settings, WebSocketHandler handler);
        ~WebSocketListener();

        WebSocketListener(const WebSocketListener&) = delete;
        WebSocketListener& operator=(const WebSocketListener&) = delete;

        /// Starts listening on @p port (0 picks a free port) and returns the port in use. The error is a message for the log (a port that is taken, for example)
        [[nodiscard]] std::expected<uint16_t, ControlError> Start(uint16_t port);

        /// Closes the listener and every connection and joins the threads. Safe to call twice and when never started
        void Stop();

        [[nodiscard]] bool IsRunning() const {
            return _listener.IsValid();
        }

        /// The port in use; 0 when the listener is not running
        [[nodiscard]] uint16_t GetPort() const {
            return _port;
        }

        /// Calls @p visit for every connection whose handshake is done and that is still open. The list is a copy: @p visit may queue frames, but it must not call Stop()
        void ForEachOpenConnection(const std::function<void(WebSocketConnection&)>& visit);

    private:
        using ConnectionPtr = WebSocketHandler::ConnectionPtr;

        void AcceptLoop();
        void AddConnection(Socket socket, String peerAddress);
        bool AdmitByClientAddress(WebSocketConnection& connection, const String& clientAddress);
        void ReapFinishedConnections();
        void RunConnection(const ConnectionPtr& connection);
        void ReadMessages(const ConnectionPtr& connection, Vector<uint8_t> pendingBytes);

        /// Acts on one message of a client; false when the connection has to end
        bool HandleMessage(const ConnectionPtr& connection, WebSocketMessage message);

        const WebSocketListenerSettings _settings;
        const WebSocketHandler _handler;
        Socket _listener;
        uint16_t _port = 0;
        std::atomic<bool> _stopping{false};
        Thread _acceptThread;
        WebSocketConnectionId _nextId = 1;  // accept thread only; not reset by Stop(), so an id is never reused

        Mutex _connectionsMutex{BE_LOCK_SITE("WebSocket connections")};
        Vector<ConnectionPtr> _connections;  // guarded by _connectionsMutex
    };

}  // namespace BECore
