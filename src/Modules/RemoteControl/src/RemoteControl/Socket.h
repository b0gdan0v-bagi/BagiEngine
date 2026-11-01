#pragma once

#include <chrono>

namespace BECore {

    /// Why a socket call did not give what it was asked for
    CORE_ENUM(SocketError, uint8_t, Closed, TimedOut, Failed)

    /**
     * @brief A TCP socket on the loopback (127.0.0.1), POSIX or Winsock behind one interface.
     *
     * Owns its handle and closes it on destruction; move-only. The calls block, so a thread that waits in
     * Accept() or Receive() is woken by ShutdownBoth() from another thread (a connected socket) or by its timeout
     * (a listening socket, see Accept()). Only IPv4 loopback is supported: the control API is a local tool.
     */
    class Socket {
    public:
        Socket() = default;
        ~Socket();

        Socket(const Socket&) = delete;
        Socket& operator=(const Socket&) = delete;
        Socket(Socket&& other) noexcept;
        Socket& operator=(Socket&& other) noexcept;

        /// Listens on 127.0.0.1:@p port; port 0 lets the system pick a free one (GetLocalPort() tells which). The error is a message for the log
        [[nodiscard]] static std::expected<Socket, eastl::string> Listen(uint16_t port);

        /// Connects to 127.0.0.1:@p port (clients and tests; the engine only listens)
        [[nodiscard]] static std::expected<Socket, eastl::string> Connect(uint16_t port);

        /// Waits up to @p timeout for a connection: TimedOut when none came, Failed when the socket is broken or was shut down
        [[nodiscard]] std::expected<Socket, SocketError> Accept(std::chrono::milliseconds timeout);

        /// Sends all of @p data; false when the connection is gone
        [[nodiscard]] bool SendAll(eastl::span<const uint8_t> data);

        /// Reads what has arrived, at most @p buffer.size() bytes (always at least one). Closed: the peer closed the connection or ShutdownBoth() was called
        [[nodiscard]] std::expected<size_t, SocketError> Receive(eastl::span<uint8_t> buffer);

        /// Receive() gives up with TimedOut after @p timeout; zero waits for ever
        bool SetReceiveTimeout(std::chrono::milliseconds timeout);

        /// Ends the connection in both directions and wakes the threads blocked in Receive() or SendAll() on it
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
    };

}  // namespace BECore
