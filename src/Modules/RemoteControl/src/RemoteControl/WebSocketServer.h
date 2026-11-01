#pragma once

#include <BECore/Control/ControlTypes.h>
#include <RemoteControl/Socket.h>
#include <RemoteControl/WebSocketFrame.h>
#include <thread>

namespace BECore {

    class ControlService;

    /// Error codes of WebSocketServer::Start()
    namespace RemoteControlErrors {
        inline constexpr eastl::string_view kListenFailed = "listen_failed";
        inline constexpr eastl::string_view kAlreadyRunning = "already_running";
    }  // namespace RemoteControlErrors

    /**
     * @brief The WebSocket transport of the control API: a server on 127.0.0.1 that feeds ControlService::Post().
     *
     * Every text message of a client is a request; the response goes back to the connection that sent it, and the
     * events of ControlService::Publish() go to every open connection. See RemoteControl/CLAUDE.md for the protocol
     * details and the limits.
     *
     * Threads: one accepts connections; each connection has a reader thread (handshake, frames, Post()) and a writer
     * thread that sends from a queue, so a client that does not read cannot stall the game: when its queue passes
     * kMaxOutboxBytes the connection is dropped. The main thread calls Start() and Stop(); responses are queued from
     * the main thread, never sent from it.
     *
     * Stop() the server before ControlService::Shutdown().
     */
    class WebSocketServer {
    public:
        /// Open connections, handshakes included; the next client gets a 503
        static constexpr size_t kMaxConnections = 4;

        /// What a connection may hold in its send queue before it is dropped
        static constexpr size_t kMaxOutboxBytes = 16 * 1024 * 1024;

        /// How long a client has to complete the handshake after it connected
        static constexpr std::chrono::milliseconds kHandshakeTimeout{5000};

        explicit WebSocketServer(ControlService& service);
        ~WebSocketServer();

        WebSocketServer(const WebSocketServer&) = delete;
        WebSocketServer& operator=(const WebSocketServer&) = delete;

        /// Starts listening on 127.0.0.1:@p port (0 picks a free port) and returns the port in use. The error is a message for the log (a port that is taken, for example)
        [[nodiscard]] std::expected<uint16_t, ControlError> Start(uint16_t port);

        /// Closes the listener and every connection and joins the threads. Safe to call twice and when never started
        void Stop();

        [[nodiscard]] bool IsRunning() const {
            return _listener.IsValid();
        }

        /// The port in use; 0 when the server is not running
        [[nodiscard]] uint16_t GetPort() const {
            return _port;
        }

    private:
        class Connection;

        void AcceptLoop();
        void AddConnection(Socket socket);
        void ReapFinishedConnections();
        void RunConnection(const eastl::shared_ptr<Connection>& connection);
        void ReadMessages(const eastl::shared_ptr<Connection>& connection, eastl::vector<uint8_t> pendingBytes);

        /// Acts on one message of a client; false when the connection has to end
        bool HandleMessage(const eastl::shared_ptr<Connection>& connection, WebSocketMessage message);
        void Broadcast(const eastl::string& text);

        ControlService& _service;
        Socket _listener;
        uint16_t _port = 0;
        std::atomic<bool> _stopping{false};
        std::thread _acceptThread;

        std::mutex _connectionsMutex;
        eastl::vector<eastl::shared_ptr<Connection>> _connections;  // guarded by _connectionsMutex
    };

}  // namespace BECore
