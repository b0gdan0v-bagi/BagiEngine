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
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace BECore {

    namespace {

        constexpr int kLoopbackListenBacklog = 8;
        constexpr int kPublicListenBacklog = 128;
        constexpr std::chrono::milliseconds kLoopbackConnectTimeout{10'000};
        constexpr const char* kUnknownPeerAddress = "?";

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

        /// A connecting socket is non-blocking on every system; on Windows it stays so afterwards
        bool EnterConnectRegime(NativeSocket handle) {
            return MakeNonBlocking(handle);
        }

        bool LeaveConnectRegime(NativeSocket) {
            return true;
        }

        bool IsConnectInProgress(int error) {
            return error == WSAEWOULDBLOCK;
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

        /// connect() with a deadline needs a non-blocking socket; Receive() and SendAll() want the blocking one back
        bool EnterConnectRegime(NativeSocket handle) {
            const int flags = fcntl(handle, F_GETFL, 0);
            return flags >= 0 && fcntl(handle, F_SETFL, flags | O_NONBLOCK) == 0;
        }

        bool LeaveConnectRegime(NativeSocket handle) {
            const int flags = fcntl(handle, F_GETFL, 0);
            return flags >= 0 && fcntl(handle, F_SETFL, flags & ~O_NONBLOCK) == 0;
        }

        bool IsConnectInProgress(int error) {
            return error == EINPROGRESS || IsInterrupted(error);
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

        sockaddr_in IPv4Address(uint32_t host, uint16_t port) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(host);
            address.sin_port = htons(port);
            return address;
        }

        sockaddr_in LoopbackAddress(uint16_t port) {
            return IPv4Address(INADDR_LOOPBACK, port);
        }

        /// The dotted text of the IPv4 address of @p address
        String FormatIPv4(const sockaddr_in& address) {
            Array<char, INET_ADDRSTRLEN> text{};
            if (inet_ntop(AF_INET, &address.sin_addr, text.data(), static_cast<socklen_t>(text.size())) == nullptr) {
                return kUnknownPeerAddress;
            }
            return text.data();
        }

        String DescribeError(StringView what, int error) {
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

        struct AddrInfoDeleter {
            void operator()(addrinfo* list) const {
                freeaddrinfo(list);
            }
        };

        /// The text of a getaddrinfo status: gai_strerror on POSIX (its codes are not errno values), the number on Windows
        String DescribeResolveError(int status) {
#ifdef PLATFORM_WINDOWS
            return Format("error {}", status);
#else
            return String(gai_strerror(status));
#endif
        }

        /// The IPv4 addresses of @p host with @p port in them, in the order the system prefers
        std::expected<Vector<sockaddr_in>, String> ResolveIPv4(const String& host, uint16_t port) {
            EnsureSocketLibrary();
            addrinfo hints{};
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_STREAM;
            addrinfo* found = nullptr;
            const int status = getaddrinfo(host.c_str(), nullptr, &hints, &found);
            UniquePtr<addrinfo, AddrInfoDeleter> list(found);
            if (status != 0 || list == nullptr) {
                return std::unexpected(Format("cannot resolve host \"{}\" ({})", host, DescribeResolveError(status != 0 ? status : EAI_NONAME)));
            }
            Vector<sockaddr_in> addresses;
            for (const addrinfo* item = list.get(); item != nullptr; item = item->ai_next) {
                if (item->ai_family == AF_INET && item->ai_addrlen >= sizeof(sockaddr_in)) {
                    sockaddr_in address{};
                    std::memcpy(&address, item->ai_addr, sizeof(address));
                    address.sin_port = htons(port);
                    addresses.push_back(address);
                }
            }
            if (addresses.empty()) {
                return std::unexpected(Format("host \"{}\" has no IPv4 address", host));
            }
            return addresses;
        }

        enum class ConnectOutcome : uint8_t { Connected, Failed, TimedOut, Cancelled };

        /// One connect() on the non-blocking @p handle, waited for in slices until @p deadline; @p error gets the system error of a failure
        ConnectOutcome ConnectNative(NativeSocket handle, const sockaddr_in& address, std::chrono::steady_clock::time_point deadline, const std::atomic<bool>* cancel, int& error) {
            if (connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) {
                return ConnectOutcome::Connected;
            }
            error = LastError();
            if (!IsConnectInProgress(error)) {
                return ConnectOutcome::Failed;
            }
            for (;;) {
                if (cancel != nullptr && cancel->load()) {
                    return ConnectOutcome::Cancelled;
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) {
                    return ConnectOutcome::TimedOut;
                }
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now) + std::chrono::milliseconds(1);
                if (!WaitFor(handle, POLLOUT, std::min(Socket::kWakeInterval, left))) {
                    continue;  // not yet (or an interrupted poll): the loop looks at the flag and the deadline again
                }
                int pending = 0;
#ifdef PLATFORM_WINDOWS
                int size = sizeof(pending);
                if (getsockopt(handle, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&pending), &size) != 0) {
#else
                socklen_t size = sizeof(pending);
                if (getsockopt(handle, SOL_SOCKET, SO_ERROR, &pending, &size) != 0) {
#endif
                    error = LastError();
                    return ConnectOutcome::Failed;
                }
                error = pending;
                return pending == 0 ? ConnectOutcome::Connected : ConnectOutcome::Failed;
            }
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

    std::expected<Socket, String> Socket::Listen(uint16_t port, SocketBind scope) {
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

        const bool isPublic = scope == SocketBind::AnyAddress;
        const sockaddr_in address = isPublic ? IPv4Address(INADDR_ANY, port) : LoopbackAddress(port);
        if (bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            return std::unexpected(DescribeError(Format("bind() to {}:{}", isPublic ? "0.0.0.0" : "127.0.0.1", port), LastError()));
        }
        if (listen(handle, isPublic ? kPublicListenBacklog : kLoopbackListenBacklog) != 0) {
            return std::unexpected(DescribeError("listen()", LastError()));
        }
        return std::move(socketGuard);
    }

    std::expected<Socket, String> Socket::Connect(uint16_t port) {
        return Connect("127.0.0.1", port, kLoopbackConnectTimeout);
    }

    std::expected<Socket, String> Socket::Connect(StringView host, uint16_t port, std::chrono::milliseconds timeout, const std::atomic<bool>* cancel) {
        const String target = Format("{}:{}", host, port);
        if (cancel != nullptr && cancel->load()) {
            return std::unexpected(Format("connect() to {} cancelled", target));
        }
        const auto addresses = ResolveIPv4(String(host), port);
        if (!addresses) {
            return std::unexpected(addresses.error());
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        String lastError;
        for (const sockaddr_in& address : *addresses) {
            const NativeSocket handle = CreateTcpSocket();
            if (handle == kNativeInvalid) {
                return std::unexpected(DescribeError("socket()", LastError()));
            }
            Socket socketGuard(static_cast<intptr_t>(handle));
            if (!EnterConnectRegime(handle)) {
                return std::unexpected(DescribeError("fcntl() or ioctlsocket()", LastError()));
            }

            int error = 0;
            switch (ConnectNative(handle, address, deadline, cancel, error)) {
                case ConnectOutcome::Connected:
                    if (!LeaveConnectRegime(handle)) {
                        return std::unexpected(DescribeError("fcntl()", LastError()));
                    }
                    return std::move(socketGuard);
                case ConnectOutcome::Cancelled:
                    return std::unexpected(Format("connect() to {} cancelled", target));
                case ConnectOutcome::TimedOut:
                    return std::unexpected(Format("connect() to {} timed out after {} ms", target, timeout.count()));
                case ConnectOutcome::Failed:
                    lastError = DescribeError(Format("connect() to {}", target), error);
                    break;  // the next address, if the name has another
            }
        }
        return std::unexpected(lastError);
    }

    std::expected<Socket, SocketError> Socket::Accept(std::chrono::milliseconds timeout, String* peerAddress) {
        if (!IsValid()) {
            return std::unexpected(SocketError::Failed);
        }
        // Closing or shutting down a listening socket does not wake accept() on every system, so the wait is a poll
        // that the caller repeats; a shut down socket makes poll() report it readable and accept() fail
        if (!WaitReadable(ToNative(_handle), timeout)) {
            return std::unexpected(SocketError::TimedOut);
        }

        for (;;) {
            sockaddr_in peer{};
#ifdef PLATFORM_WINDOWS
            int peerSize = sizeof(peer);
#else
            socklen_t peerSize = sizeof(peer);
#endif
            const NativeSocket accepted = accept(ToNative(_handle), reinterpret_cast<sockaddr*>(&peer), &peerSize);
            if (accepted != kNativeInvalid) {
                if (peerAddress != nullptr) {
                    const bool known = peer.sin_family == AF_INET && peerSize >= static_cast<decltype(peerSize)>(sizeof(peer));
                    *peerAddress = known ? FormatIPv4(peer) : String(kUnknownPeerAddress);
                }
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

    bool Socket::SendAll(Span<const uint8_t> data) {
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

    std::expected<size_t, SocketError> Socket::Receive(Span<uint8_t> buffer) {
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
