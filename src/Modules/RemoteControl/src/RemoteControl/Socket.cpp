#include "Socket.h"

#ifdef PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace BECore {

    namespace {

        constexpr int kListenBacklog = 8;

#ifdef PLATFORM_WINDOWS
        using NativeSocket = SOCKET;
        constexpr NativeSocket kNativeInvalid = INVALID_SOCKET;

        int LastError() {
            return WSAGetLastError();
        }

        /// The socket is non-blocking and has no SO_RCVTIMEO, so WSAETIMEDOUT would mean a dropped connection, not a wait
        bool IsTimeout(int error) {
            return error == WSAEWOULDBLOCK;
        }

        bool IsInterrupted(int) {
            return false;
        }

        /// Winsock has to be started once before the first socket; it stays up until the process ends
        void EnsureSocketLibrary() {
            static const bool started = [] {
                WSADATA data{};
                return WSAStartup(MAKEWORD(2, 2), &data) == 0;
            }();
            (void)started;
        }

        void CloseNative(NativeSocket handle) {
            closesocket(handle);
        }

        /// A connected socket is non-blocking on Windows: shutdown() does not wake a blocked recv() or send() there
        bool MakeNonBlocking(NativeSocket handle) {
            u_long enable = 1;
            return ioctlsocket(handle, FIONBIO, &enable) == 0;
        }

        constexpr int kSendFlags = 0;
        constexpr int kShutdownBoth = SD_BOTH;
#else
        using NativeSocket = int;
        constexpr NativeSocket kNativeInvalid = -1;

        int LastError() {
            return errno;
        }

        bool IsTimeout(int error) {
            return error == EAGAIN || error == EWOULDBLOCK;
        }

        bool IsInterrupted(int error) {
            return error == EINTR;
        }

        void EnsureSocketLibrary() {}

        /// Nothing to do on POSIX: a connected socket stays blocking there and shutdown() wakes it (the name is the Windows need)
        bool MakeNonBlocking(NativeSocket) {
            return true;
        }

        void CloseNative(NativeSocket handle) {
            close(handle);
        }

#ifdef PLATFORM_MACOS
        constexpr int kSendFlags = 0;  // no MSG_NOSIGNAL on macOS: SO_NOSIGPIPE is set on every socket instead
#else
        constexpr int kSendFlags = MSG_NOSIGNAL;
#endif
        constexpr int kShutdownBoth = SHUT_RDWR;
#endif

        NativeSocket ToNative(intptr_t handle) {
            return static_cast<NativeSocket>(handle);
        }

        sockaddr_in LoopbackAddress(uint16_t port) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            return address;
        }

        eastl::string DescribeError(eastl::string_view what, int error) {
            return Format("{} failed (error {})", what, error);
        }

        /// A new TCP socket that does not raise SIGPIPE when the peer is gone; the handle is invalid when it could not be made
        NativeSocket CreateTcpSocket() {
            EnsureSocketLibrary();
            const NativeSocket handle = socket(AF_INET, SOCK_STREAM, 0);
#ifdef PLATFORM_MACOS
            if (handle != kNativeInvalid) {
                const int enable = 1;
                setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
            }
#endif
            return handle;
        }

        /// Waits until @p handle has one of @p events (POLLIN, POLLOUT) for at most @p timeout; false on timeout or on an error
        bool WaitFor(NativeSocket handle, short events, std::chrono::milliseconds timeout) {
            pollfd descriptor{};
            descriptor.fd = handle;
            descriptor.events = events;
            const int milliseconds = static_cast<int>(timeout.count());
#ifdef PLATFORM_WINDOWS
            return WSAPoll(&descriptor, 1, milliseconds) > 0;
#else
            return poll(&descriptor, 1, milliseconds) > 0;
#endif
        }

        /// Waits until @p handle can be read (a connection to accept) for at most @p timeout; false on timeout or on an error
        bool WaitReadable(NativeSocket handle, std::chrono::milliseconds timeout) {
            return WaitFor(handle, POLLIN, timeout);
        }

    }  // namespace

    Socket::~Socket() {
        Close();
    }

    Socket::Socket(Socket&& other) noexcept : _handle(std::exchange(other._handle, kInvalidHandle)), _receiveTimeout(other._receiveTimeout), _shutDown(other._shutDown.load()) {}

    Socket& Socket::operator=(Socket&& other) noexcept {
        if (this != &other) {
            Close();
            _handle = std::exchange(other._handle, kInvalidHandle);
            _receiveTimeout = other._receiveTimeout;
            _shutDown = other._shutDown.load();
        }
        return *this;
    }

    std::expected<Socket, eastl::string> Socket::Listen(uint16_t port) {
        const NativeSocket handle = CreateTcpSocket();
        if (handle == kNativeInvalid) {
            return std::unexpected(DescribeError("socket()", LastError()));
        }
        Socket socketGuard(static_cast<intptr_t>(handle));

#ifdef PLATFORM_WINDOWS
        // SO_REUSEADDR on Windows would let another process take the port over; the exclusive flag refuses a taken port
        const BOOL exclusive = TRUE;
        setsockopt(handle, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
        const int reuse = 1;
        setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        const sockaddr_in address = LoopbackAddress(port);
        if (bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return std::unexpected(DescribeError(Format("bind() to 127.0.0.1:{}", port), LastError()));
        }
        if (listen(handle, kListenBacklog) != 0) {
            return std::unexpected(DescribeError("listen()", LastError()));
        }
        return std::move(socketGuard);
    }

    std::expected<Socket, eastl::string> Socket::Connect(uint16_t port) {
        const NativeSocket handle = CreateTcpSocket();
        if (handle == kNativeInvalid) {
            return std::unexpected(DescribeError("socket()", LastError()));
        }
        Socket socketGuard(static_cast<intptr_t>(handle));

        const sockaddr_in address = LoopbackAddress(port);
        if (connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return std::unexpected(DescribeError(Format("connect() to 127.0.0.1:{}", port), LastError()));
        }
        if (!MakeNonBlocking(handle)) {
            return std::unexpected(DescribeError("ioctlsocket()", LastError()));
        }
        return std::move(socketGuard);
    }

    std::expected<Socket, SocketError> Socket::Accept(std::chrono::milliseconds timeout) {
        if (!IsValid()) {
            return std::unexpected(SocketError::Failed);
        }
        // Closing or shutting down a listening socket does not wake accept() on every system, so the wait is a poll
        // that the caller repeats; a shut down socket makes poll() report it readable and accept() fail
        if (!WaitReadable(ToNative(_handle), timeout)) {
            return std::unexpected(SocketError::TimedOut);
        }

        for (;;) {
            const NativeSocket accepted = accept(ToNative(_handle), nullptr, nullptr);
            if (accepted != kNativeInvalid) {
#ifdef PLATFORM_MACOS
                const int enable = 1;
                setsockopt(accepted, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif
                Socket connection(static_cast<intptr_t>(accepted));  // closes the handle if the next call fails
                if (!MakeNonBlocking(accepted)) {
                    return std::unexpected(SocketError::Failed);
                }
                return connection;
            }
            if (!IsInterrupted(LastError())) {
                return std::unexpected(SocketError::Failed);
            }
        }
    }

    bool Socket::SendAll(eastl::span<const uint8_t> data) {
        if (!IsValid()) {
            return false;
        }
        size_t sent = 0;
        while (sent < data.size()) {
            if (_shutDown) {
                return false;
            }
            // Winsock takes an int length, POSIX a size_t: stay inside int for both
            const auto chunk = static_cast<int>(std::min<size_t>(data.size() - sent, std::numeric_limits<int>::max()));
            const auto written = send(ToNative(_handle), reinterpret_cast<const char*>(data.data() + sent), chunk, kSendFlags);
            if (written < 0) {
                const int error = LastError();
                if (IsInterrupted(error)) {
                    continue;
                }
                if (_shutDown) {
                    return false;  // ShutdownBoth() came between the check and send()
                }
#ifdef PLATFORM_WINDOWS
                if (IsTimeout(error)) {
                    WaitFor(ToNative(_handle), POLLOUT, kWakeInterval);  // the send buffer is full: wait for the client to read
                    continue;
                }
#endif
                return false;
            }
            sent += static_cast<size_t>(written);
        }
        return true;
    }

    std::expected<size_t, SocketError> Socket::Receive(eastl::span<uint8_t> buffer) {
        if (!IsValid() || buffer.empty()) {
            return std::unexpected(SocketError::Failed);
        }
#ifdef PLATFORM_WINDOWS
        const auto started = std::chrono::steady_clock::now();
#endif
        for (;;) {
            if (_shutDown) {
                return std::unexpected(SocketError::Closed);
            }
            const auto length = static_cast<int>(std::min<size_t>(buffer.size(), std::numeric_limits<int>::max()));
            const auto received = recv(ToNative(_handle), reinterpret_cast<char*>(buffer.data()), length, 0);
            if (received > 0) {
                return static_cast<size_t>(received);
            }
            if (received == 0) {
                return std::unexpected(SocketError::Closed);
            }
            const int error = LastError();
            if (IsInterrupted(error)) {
                continue;
            }
            if (_shutDown) {
                return std::unexpected(SocketError::Closed);  // ShutdownBoth() came between the check and recv()
            }
            if (!IsTimeout(error)) {
                return std::unexpected(SocketError::Failed);
            }
#ifdef PLATFORM_WINDOWS
            // Non-blocking: wait here, in slices, so that ShutdownBoth() from another thread is noticed
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
            if (_receiveTimeout > std::chrono::milliseconds::zero() && waited >= _receiveTimeout) {
                return std::unexpected(SocketError::TimedOut);
            }
            const auto slice = _receiveTimeout > std::chrono::milliseconds::zero() ? std::min(kWakeInterval, _receiveTimeout - waited) : kWakeInterval;
            WaitFor(ToNative(_handle), POLLIN, slice);
#else
            return std::unexpected(SocketError::TimedOut);  // SO_RCVTIMEO ran out
#endif
        }
    }

    bool Socket::SetReceiveTimeout(std::chrono::milliseconds timeout) {
        if (!IsValid()) {
            return false;
        }
        _receiveTimeout = timeout;
#ifdef PLATFORM_WINDOWS
        // Receive() keeps the timeout itself on Windows (the socket is non-blocking)
        return true;
#else
        timeval value{};
        value.tv_sec = static_cast<decltype(value.tv_sec)>(timeout.count() / 1000);
        value.tv_usec = static_cast<decltype(value.tv_usec)>((timeout.count() % 1000) * 1000);
        return setsockopt(ToNative(_handle), SOL_SOCKET, SO_RCVTIMEO, &value, sizeof(value)) == 0;
#endif
    }

    void Socket::ShutdownBoth() {
        if (IsValid()) {
            _shutDown = true;
            shutdown(ToNative(_handle), kShutdownBoth);
        }
    }

    void Socket::Close() {
        if (IsValid()) {
            CloseNative(ToNative(_handle));
            _handle = kInvalidHandle;
        }
    }

    uint16_t Socket::GetLocalPort() const {
        if (!IsValid()) {
            return 0;
        }
        sockaddr_in address{};
#ifdef PLATFORM_WINDOWS
        int size = sizeof(address);
#else
        socklen_t size = sizeof(address);
#endif
        if (getsockname(ToNative(_handle), reinterpret_cast<sockaddr*>(&address), &size) != 0) {
            return 0;
        }
        return ntohs(address.sin_port);
    }

}  // namespace BECore
