#pragma once

#include <chrono>

namespace BECore {

    /// Why a socket call did not give what it was asked for
    CORE_ENUM(SocketError, uint8_t, Closed, TimedOut, Failed)

    /// Which local addresses a listening socket accepts connections on
    CORE_ENUM(SocketBind, uint8_t, Loopback, AnyAddress)

    /// The bytes of @p text, for SendAll()
    [[nodiscard]] inline Span<const uint8_t> AsBytes(StringView text) {
        return Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    /**
     * @brief A TCP socket (IPv4), POSIX or Winsock behind one interface.
     *
     * Owns its handle and closes it on destruction; move-only. On POSIX the calls block, so a thread that waits in
     * Accept() or Receive() is woken by ShutdownBoth() from another thread (a connected socket) or by its timeout
     * (a listening socket, see Accept()). A listening socket binds the loopback (the control API is a local tool) or,
     * for a public server, every address (SocketBind); only IPv4 is supported. Connect() reaches any IPv4 host.
     *
     * Winsock does not wake a blocked recv() or send() on shutdown(), so there a connected socket is non-blocking
     * and Receive() / SendAll() wait in slices of kWakeInterval that look for ShutdownBoth(); the interface is the same.
     */
    class Socket {
    public:
        /// How often a waiting Receive() or SendAll() looks for ShutdownBoth() where the system does not wake it (Windows)
        static constexpr std::chrono::milliseconds kWakeInterval{50};

        Socket() = default;
        ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
        Socket(Socket&& other) noexcept;
        Socket& operator=(Socket&& other) noexcept;

        /// Listens on @p port, on 127.0.0.1 only or on every address (@p scope); port 0 lets the system pick a free one
        /// (GetLocalPort() tells which). The error is a message for the log and names the address used
        [[nodiscard]] static std::expected<Socket, String> Listen(uint16_t port, SocketBind scope = SocketBind::Loopback);

        /// Connects to 127.0.0.1:@p port (a loopback client of tests and tools); see the overload below
        [[nodiscard]] static std::expected<Socket, String> Connect(uint16_t port);

        /// Connects to @p host (a name or an IPv4 literal) and @p port. The name is resolved by the system (blocking, not
        /// bounded by @p timeout); the connection itself waits in slices of kWakeInterval, so an error "cancelled" follows
        /// within one slice after @p cancel (read from this thread, set from another) became true, and "timed out" after
        /// @p timeout. The error is a message for the log and names host:port
        [[nodiscard]] static std::expected<Socket, String> Connect(StringView host, uint16_t port, std::chrono::milliseconds timeout, const std::atomic<bool>* cancel = nullptr);

        /// Waits up to @p timeout for a connection: TimedOut when none came, Failed when the socket is broken or was shut down.
        /// @p peerAddress, when given, gets the IPv4 address of the client as dotted text ("?" when it is unknown)
        [[nodiscard]] std::expected<Socket, SocketError> Accept(std::chrono::milliseconds timeout, String* peerAddress = nullptr);

        /// Sends all of @p data; false when the connection is gone
        [[nodiscard]] bool SendAll(Span<const uint8_t> data);

        /// Reads what has arrived, at most @p buffer.size() bytes (always at least one). Closed: the peer closed the connection or ShutdownBoth() was called
        [[nodiscard]] std::expected<size_t, SocketError> Receive(Span<uint8_t> buffer);

        /// Receive() gives up with TimedOut after @p timeout; zero waits for ever
        bool SetReceiveTimeout(std::chrono::milliseconds timeout);

        /// Ends the connection in both directions and wakes the threads blocked in Receive() or SendAll() on it (on Windows within kWakeInterval)
        void ShutdownBoth();

        void Close();

        [[nodiscard]] bool IsValid() const {
            return _handle != kInvalidHandle;
        }

        /// The port this socket is bound to or connected from; 0 when it is not valid
        [[nodiscard]] uint16_t GetLocalPort() const;

    private:
        static constexpr intptr_t kInvalidHandle = -1;

        explicit Socket(intptr_t handle) : _handle(handle) {}

        intptr_t _handle = kInvalidHandle;
        std::chrono::milliseconds _receiveTimeout = std::chrono::milliseconds::zero();  // zero: wait for ever
        std::atomic<bool> _shutDown{false};                                             // set by ShutdownBoth() from any thread, read by the thread that waits
    };

}  // namespace BECore
