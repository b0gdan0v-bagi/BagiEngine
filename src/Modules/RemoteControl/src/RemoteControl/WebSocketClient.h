#pragma once

#include <RemoteControl/Socket.h>
#include <RemoteControl/Tls/ClientStream.h>
#include <RemoteControl/WebSocketClientHandshake.h>
#include <RemoteControl/WebSocketFrame.h>
#include <random>

namespace BECore {

    /// The old name of kTlsUnsupported (Tls/TlsStream.h): the error text of a `wss://` address on a platform without a TLS backend starts with it
    inline constexpr StringView kWebSocketTlsUnsupported = kTlsUnsupported;

    /// The limits, timeouts and names of one client
    struct WebSocketClientSettings {
        /// How long the TCP connect may take (the name lookup before it is not bounded)
        std::chrono::milliseconds connectTimeout{10000};

        /// How long the opening may take. With `ws://` it counts from the connected socket (the server's answer to the opening request); with `wss://` it counts from the start
        /// of the connect, so the name lookup, the TCP connect, the TLS handshake and the answer share it
        std::chrono::milliseconds handshakeTimeout{10000};

        /// After Close() or after answering the server's close: how long to wait for the other side before the connection is aborted
        std::chrono::milliseconds closeTimeout{2000};

        /// A longer message of the server closes the connection with 1009, at the frame header, before it is buffered
        size_t maxMessageBytes = 1024 * 1024;

        /// What the send queue may hold; SendText() says no above it (and a queue that does not drain drops the connection)
        size_t maxOutboxBytes = 1024 * 1024;

        /// What PollEvent() has not taken yet: above either limit the reader waits (the server's send queue fills, TCP slows it)
        size_t maxInboxEvents = 1024;
        size_t maxInboxBytes = 4 * 1024 * 1024;

        /// The `Origin` header of the opening request; empty sends none (a program is not a page)
        String origin;

        /// The prefix of the log lines
        String logName = "WebSocket client";

        /// The names of the threads in the profiler: `<threadName> reader` and `writer`, sorted from `threadOrder`
        String threadName = "WebSocket client";
        uint32_t threadOrder = 220;

        /// `wss://`: the extra trust anchors and, for a test, the backend (empty: this platform's)
        TlsClientOptions tls;
    };

    CORE_ENUM(WebSocketClientEventKind, uint8_t, Opened, Text, Closed)

    /// One thing that happened on the connection, in the order it happened
    struct WebSocketClientEvent {
        WebSocketClientEventKind kind = WebSocketClientEventKind::Closed;
        String text;  // Text: the message
        /// Closed: the code of the close frame that ended the connection (the server's, or the one this side sent: a protocol
        /// error, Close()); 1006 when the connection ended without one (refused, dropped, aborted, a handshake that failed)
        uint16_t closeCode = WebSocketCloseCode::kAbnormalClosure;
        String reason;  // Closed: why, for the log and for a message to the player
    };

    /// Where a WebSocketClient is in its life (internal; the states of the connection)
    CORE_ENUM(WebSocketClientState, uint8_t, Idle, Connecting, Open, Closing, Ended)

    /**
     * @brief A WebSocket client over a plain TCP socket (`ws://`) or a TLS stream (`wss://`, Tls/): the transport of a game that talks to a server.
     *
     * Open() starts a reader thread that connects, does the opening handshake and then reads; a writer thread sends from a
     * queue, so SendText() never waits for the network. **Nothing is called back from these threads**: everything that
     * happens is an event that the owner takes with PollEvent() from its own thread (the main thread of a game, once per
     * frame), so the owner needs no lock.
     *
     * Exactly one Closed event follows every Open() that returned a value, whatever ends the connection (a refused connect
     * and a failed handshake included, with code 1006), and it is the last event: every message that arrived before is
     * delivered first. The threads are joined by the next Open() or the destructor, which aborts, wakes every wait and joins
     * (safe in any state, also for an object that never opened; a reader still inside getaddrinfo cannot be woken, so the join may
     * wait for that one name lookup). One object serves one connection at a time.
     *
     * The client masks every frame with a fresh key, answers pings, answers a close with a close, and closes the connection
     * with 1002 on a protocol error (a masked frame of the server included), 1009 on a message over the limit, 1003 on a
     * binary message. `wss://` goes through ITlsStream (the certificate is always checked: Tls/CLAUDE.md); a failed TLS connect is a Closed 1006 whose
     * reason says what failed, and on a platform without a backend Open() refuses it at once (kTlsUnsupported). Only the thread that calls Open(), SendText(), Close(),
     * Abort() and PollEvent() may do so (one thread, the owner's); IsOpen() and GetBytesReceived() are safe from any.
     */
    class WebSocketClient {
    public:
        WebSocketClient() = default;
        ~WebSocketClient();

        WebSocketClient(const WebSocketClient&) = delete;
        WebSocketClient& operator=(const WebSocketClient&) = delete;

        /// Starts connecting to @p url. The error is a message for the log and means no thread was started and no Closed will
        /// come: a URL that does not parse, `wss://` on a platform without a TLS backend (starts with kTlsUnsupported), a previous connection that has not
        /// ended yet. Events of a previous connection that were not taken are dropped
        [[nodiscard]] std::expected<void, String> Open(StringView url, WebSocketClientSettings settings = {});

        /// Queues a text message; false when the connection is not open, is closing, or the queue is full (over the cap, and a
        /// queue that holds something already means the server does not read: the connection is then dropped)
        bool SendText(StringView text);

        /// Starts the closing handshake: the close frame follows what is queued, and if the server does not answer within
        /// closeTimeout the connection is aborted. During the connect it is the same as Abort(). Closed carries @p code and @p reason
        /// unless the server closed first
        void Close(uint16_t code = WebSocketCloseCode::kNormal, StringView reason = {});

        /// Ends the connection now, without a close frame; wakes every wait. Closed (1006) still follows
        void Abort();

        /// Takes the next event; false when there is none. Does not allocate when there is none
        [[nodiscard]] bool PollEvent(WebSocketClientEvent& event);

        /// True from the end of the handshake until the connection is closing or has ended
        [[nodiscard]] bool IsOpen() const;

        /// Bytes that arrived from the server since Open(), the handshake included
        [[nodiscard]] uint64_t GetBytesReceived() const {
            return _bytesReceived.load(std::memory_order_relaxed);
        }

    private:
        struct OutgoingFrame {
            uint8_t opcode = WebSocketOpcode::kText;
            Vector<uint8_t> payload;
            bool isClose = false;
        };

        bool QueueFrame(uint8_t opcode, Vector<uint8_t> payload, bool isClose);
        void AbortConnection(uint16_t code, StringView reason);
        /// Records how the connection ended unless something did already (the first reason is the true one); _mutex held
        void SetEndLocked(uint16_t code, StringView reason);
        void SetEnd(uint16_t code, StringView reason);
        void PushEvent(WebSocketClientEvent event);
        /// @p what, with the reason the stream gives (a TLS failure) after a colon when it has one
        String WithStreamError(StringView what) const;

        void ReaderMain();
        bool Connect();
        bool Handshake(Vector<uint8_t>& pendingBytes);
        void ReadMessages(Vector<uint8_t> pendingBytes);
        bool HandleMessage(WebSocketMessage message);
        bool PushText(WebSocketMessage& message);
        void WriterMain();

        // Set by Open() before the reader starts and not changed until the reader has ended
        WebSocketClientSettings _settings;
        WebSocketUrl _url;

        Thread _reader;                    // started by Open(), joined by the next Open() or the destructor
        Thread _writer;                    // started and joined by the reader
        std::random_device _random;        // the reader draws the handshake key, then the writer the masks: never both at once
        std::atomic<bool> _cancel{false};  // wakes the connect and the TLS handshake
        std::atomic<uint64_t> _bytesReceived{0};

        ClientStream _stream;                                      // published by the reader under _mutex once connected; closed only after the threads were joined
        std::chrono::steady_clock::time_point _handshakeDeadline;  // the reader's: the end of the opening (with wss:// since the start of the connect)

        mutable Mutex _mutex{BE_LOCK_SITE("WebSocket client")};  // guards everything below
        ConditionVariable _wake;                                 // the writer: a frame was queued, the connection was aborted, the reader ended
        ConditionVariable _inboxSpace;                           // the reader: PollEvent() made room, or the connection was aborted
        WebSocketClientState _state = WebSocketClientState::Idle;
        bool _aborted = false;
        bool _readerDone = false;  // the reader will read no more: a writer that sent its close frame need not wait for the answer
        bool _writerDone = false;
        eastl::deque<OutgoingFrame> _outbox;
        size_t _outboxBytes = 0;
        eastl::deque<WebSocketClientEvent> _inbox;
        size_t _inboxBytes = 0;
        bool _endSet = false;
        uint16_t _endCode = WebSocketCloseCode::kAbnormalClosure;
        String _endReason;
    };

}  // namespace BECore
