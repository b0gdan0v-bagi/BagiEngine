#pragma once

#include <RemoteControl/Socket.h>
#include <RemoteControl/WebSocketFrame.h>
#include <RemoteControl/WebSocketHandshake.h>
#include <condition_variable>
#include <thread>

namespace BECore {

    /// A connection of a WebSocketListener; numbered by the listener from 1, never reused while the listener lives
    using WebSocketConnectionId = uint64_t;

    /// What a connection takes from the listener that made it (copied, because a connection can outlive the listener's settings)
    struct WebSocketConnectionSettings {
        /// What the send queue may hold before the connection is dropped
        size_t maxOutboxBytes = 1024 * 1024;

        /// The prefix of the log lines of this connection
        eastl::string logName = "WebSocket";

        /// The text of the 503 a client gets when AdmitClientFn refuses it
        eastl::string busyMessage = "the server has too many connections";

        /// Name and sort index of the writer thread in the profiler
        eastl::string writerThreadName = "WebSocket writer";
        uint32_t writerThreadOrder = 202;
    };

    /// Asked once per handshake of a server behind trusted proxies (HandshakePolicy::trustedProxyHops), after the request is accepted
    /// and before the 101, with the client address the proxies report. False: the client is answered with a 503 and no connection
    /// opens. It runs on the reader thread, and it has to call SetClientAddress() itself (so the check and the address are one step)
    using AdmitClientFn = std::function<bool(const eastl::string& clientAddress)>;

    /**
     * @brief One client of a WebSocketListener: its socket, the two threads that serve it and the queue of frames waiting to be sent.
     *
     * The reader thread is started and joined by the listener; it does the handshake, starts the writer and reads. The writer thread
     * sends from the queue, so a caller of the Queue functions (any thread) never blocks on the network, and a client that does not
     * read cannot stall anybody: when its queue passes the cap the connection is dropped. The Queue functions are safe from any
     * thread and after the connection ended (they take nothing then), so a holder of a shared_ptr may call them late.
     */
    class WebSocketConnection {
    public:
        WebSocketConnection(WebSocketConnectionId id, Socket socket, eastl::string peerAddress, WebSocketConnectionSettings settings);

        WebSocketConnection(const WebSocketConnection&) = delete;
        WebSocketConnection& operator=(const WebSocketConnection&) = delete;

        [[nodiscard]] WebSocketConnectionId GetId() const {
            return _id;
        }

        /// The IPv4 address of the socket's peer as dotted text ("?" when it is unknown); a proxy's, for a server behind one
        [[nodiscard]] const eastl::string& GetPeerAddress() const {
            return _peerAddress;
        }

        /// The address of the client for the per-address rules: the peer address until the handshake of a server behind trusted
        /// proxies has set the one they report. Any thread
        [[nodiscard]] eastl::string GetClientAddress() const;

        /// True once SetClientAddress() was called: the handshake of a server behind trusted proxies has settled who the client is
        [[nodiscard]] bool HasClientAddress() const;

        /// Sets the address that GetClientAddress() returns (see AdmitClientFn); once, by the reader thread during the handshake
        void SetClientAddress(eastl::string address);

        /// How many bytes arrived from the client so far, the handshake included. Grows with every receive, so a caller that
        /// reads it twice learns whether the client sent anything in between (a pong counts)
        [[nodiscard]] uint64_t GetBytesReceived() const {
            return _bytesReceived.load(std::memory_order_relaxed);
        }

        /// True from the end of the handshake until the connection is closing or dropped
        [[nodiscard]] bool IsOpen() const;

        /// Reads what has arrived (reader thread only); counts it in GetBytesReceived()
        [[nodiscard]] std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer);

        /// Reads the HTTP request, answers it and leaves in @p pendingBytes what came after it. With policy.trustedProxyHops > 0 the
        /// client address the proxies report is found first and passed to @p admit (when there is one; otherwise it is simply set).
        /// False: no WebSocket connection (the client was answered with an error or is gone)
        bool Handshake(const HandshakePolicy& policy, std::chrono::milliseconds timeout, eastl::vector<uint8_t>& pendingBytes, const AdmitClientFn& admit = {});

        void StartReader(std::thread reader) {
            _reader = std::move(reader);
        }

        /// Joins the reader thread; the reader joins the writer before it ends
        void Join() {
            if (_reader.joinable()) {
                _reader.join();
            }
        }

        void MarkFinished() {
            _finished = true;
        }

        [[nodiscard]] bool IsFinished() const {
            return _finished;
        }

        /// Opens the queue and starts the writer thread (reader thread, after the handshake)
        void StartWriter();

        /// Called by the reader when it is done: a queued close frame is sent, anything else is dropped; then the writer ends
        void EndReading();

        /// Queues a text frame; false when the connection takes no more frames
        bool QueueText(eastl::string_view text) {
            return QueueFrame(EncodeTextFrame(text), /*isClose=*/false);
        }

        bool QueuePing(eastl::span<const uint8_t> payload = {}) {
            return QueueFrame(EncodeFrame(WebSocketOpcode::kPing, payload), /*isClose=*/false);
        }

        bool QueuePong(eastl::span<const uint8_t> payload) {
            return QueueFrame(EncodeFrame(WebSocketOpcode::kPong, payload), /*isClose=*/false);
        }

        /// Queues the close frame; the connection ends after it was sent
        bool QueueClose(uint16_t code, eastl::string_view reason) {
            return QueueFrame(EncodeCloseFrame(code, reason), /*isClose=*/true);
        }

        /// Drops what is queued and ends both directions now; wakes the threads that wait on the socket or the queue
        void Abort();

    private:
        struct OutgoingFrame {
            eastl::vector<uint8_t> bytes;
            bool isClose = false;
        };

        bool QueueFrame(eastl::vector<uint8_t> bytes, bool isClose);
        void WriterLoop();

        const WebSocketConnectionId _id;
        const eastl::string _peerAddress;
        const WebSocketConnectionSettings _settings;

        Socket _socket;
        std::thread _reader;  // started and joined by the listener (accept thread and Stop()), never touched by the connection's own threads
        std::thread _writer;  // started and joined by the reader thread
        std::atomic<bool> _finished{false};
        std::atomic<uint64_t> _bytesReceived{0};

        mutable std::mutex _mutex;  // guards everything below
        std::condition_variable _wake;
        eastl::deque<OutgoingFrame> _outbox;
        size_t _outboxBytes = 0;
        bool _open = false;     // handshake done, the writer runs
        bool _closing = false;  // a close frame is queued: nothing may follow it
        bool _aborted = false;
        eastl::string _clientAddress;  // empty: the peer address
    };

}  // namespace BECore
