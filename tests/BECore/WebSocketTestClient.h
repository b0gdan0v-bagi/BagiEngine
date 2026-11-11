#pragma once

#include <RemoteControl/Socket.h>
#include <RemoteControl/WebSocketFrame.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    /// A raw WebSocket client for the tests of the servers (WebSocketServerTest, WebSocketListenerTest, the Rooms socket tests):
    /// the handshake and the frames are written by hand, so a server is tested against the bytes of the RFC.

    inline constexpr WebSocketMask kClientMask = {0x12, 0x34, 0x56, 0x78};
    inline constexpr const char* kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";
    inline constexpr std::chrono::seconds kClientWaitLimit{30};

    /// Something a test thread does while it waits (the server tests drain the control service); may be empty
    using WaitPump = std::function<void()>;

    /// Calls @p pump and @p done every millisecond until @p done is true or @p limit has passed
    template <typename Predicate>
    bool WaitUntil(const WaitPump& pump, Predicate done, std::chrono::milliseconds limit = kClientWaitLimit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (pump) {
                pump();
            }
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    inline eastl::span<const uint8_t> AsBytes(const std::string& text) {
        return eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    inline eastl::span<const uint8_t> AsBytes(const eastl::vector<uint8_t>& bytes) {
        return eastl::span<const uint8_t>(bytes.data(), bytes.size());
    }

    /// The opening handshake of the RFC for a server on @p port; @p host replaces the `Host` header value when it is not empty
    inline std::string HandshakeRequest(uint16_t port, const std::string& extraHeaders = "", const std::string& host = "") {
        const std::string hostValue = host.empty() ? "127.0.0.1:" + std::to_string(port) : host;
        return "GET /control HTTP/1.1\r\nHost: " + hostValue + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + std::string(kClientKey) + "\r\nSec-WebSocket-Version: 13\r\n" +
               extraHeaders + "\r\n";
    }

    /// A frame of the server (not masked)
    struct ServerFrame {
        uint8_t opcode = 0;
        std::string payload;
    };

    inline uint16_t CloseCodeOf(const ServerFrame& frame) {
        return static_cast<uint16_t>((static_cast<uint8_t>(frame.payload[0]) << 8) | static_cast<uint8_t>(frame.payload[1]));
    }

    /// The reason of a close frame (after the two bytes of the code)
    inline std::string CloseReasonOf(const ServerFrame& frame) {
        return frame.payload.size() > 2 ? frame.payload.substr(2) : std::string();
    }

    class WebSocketTestClient {
    public:
        /// Connects to 127.0.0.1:@p port (no handshake yet). @p pump runs while the client waits
        static std::optional<WebSocketTestClient> Open(uint16_t port, WaitPump pump = {}) {
            auto socket = Socket::Connect(port);
            if (!socket) {
                ADD_FAILURE() << "connect failed: " << socket.error().c_str();
                return std::nullopt;
            }
            WebSocketTestClient client;
            client._socket = std::move(*socket);
            client._socket.SetReceiveTimeout(std::chrono::milliseconds(5));
            client._pump = std::move(pump);
            return client;
        }

        void Send(eastl::span<const uint8_t> bytes) {
            EXPECT_TRUE(_socket.SendAll(bytes));
        }

        void Send(const std::string& text) {
            Send(AsBytes(text));
        }

        void SendText(const std::string& text) {
            Send(AsBytes(EncodeTextFrame(eastl::string_view(text.data(), text.size()), kClientMask)));
        }

        /// The response head of the handshake, and the body when it says Content-Length. Empty when nothing came in time
        std::string ReadHttpResponse() {
            constexpr std::string_view kHeadEnd = "\r\n\r\n";
            std::string response;
            size_t bodyExpected = 0;
            const bool complete = WaitUntil(_pump, [&] {
                FillBuffer();
                const std::string text(reinterpret_cast<const char*>(_buffer.data()), _buffer.size());
                const size_t headEnd = text.find(kHeadEnd);
                if (headEnd == std::string::npos) {
                    return false;
                }
                const size_t lengthAt = text.find("Content-Length: ");
                bodyExpected = (lengthAt == std::string::npos || lengthAt > headEnd) ? 0 : static_cast<size_t>(std::stoul(text.substr(lengthAt + 16)));
                if (text.size() < headEnd + kHeadEnd.size() + bodyExpected) {
                    return false;
                }
                response = text.substr(0, headEnd + kHeadEnd.size() + bodyExpected);
                _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(response.size()));
                return true;
            });
            return complete ? response : std::string();
        }

        /// Performs the handshake of the RFC and returns whether the server switched protocols
        bool Handshake(uint16_t port, const std::string& extraHeaders = "") {
            Send(HandshakeRequest(port, extraHeaders));
            return ReadHttpResponse().starts_with("HTTP/1.1 101 ");
        }

        /// The next complete frame of the server, nullopt when none came yet
        std::optional<ServerFrame> TryReadFrame() {
            if (const auto frame = ParseBuffered()) {
                return frame;
            }
            FillBuffer();
            return ParseBuffered();
        }

        /// Waits up to @p limit for the next frame
        std::optional<ServerFrame> WaitForFrame(std::chrono::milliseconds limit = kClientWaitLimit) {
            std::optional<ServerFrame> frame;
            WaitUntil(
                _pump,
                [&] {
                    frame = TryReadFrame();
                    return frame.has_value();
                },
                limit);
            return frame;
        }

        /// True when the server ended the connection (and every frame it sent before has been read)
        bool WaitForEndOfStream() {
            return WaitUntil(_pump, [&] {
                FillBuffer();
                return _ended;
            });
        }

        /// Ends the connection from the client's side at once, as a client that vanished
        void Drop() {
            _socket.ShutdownBoth();
            _socket.Close();
        }

    private:
        static constexpr int kMaxChunksPerFill = 1024;

        WebSocketTestClient() = default;

        /// Reads everything that has arrived (up to kMaxChunksPerFill chunks); a loop, because WaitUntil sleeps 1 ms
        /// between its calls, which is 15 ms on Windows, and a megabyte in chunks of 4 KiB would take seconds
        void FillBuffer() {
            std::array<uint8_t, 4096> chunk;
            for (int i = 0; i < kMaxChunksPerFill; ++i) {
                const auto received = _socket.Receive(eastl::span<uint8_t>(chunk.data(), chunk.size()));
                if (!received) {
                    if (received.error() != SocketError::TimedOut) {
                        _ended = true;
                    }
                    return;
                }
                // Pointers, not std::array iterators: EASTL's insert does not take MSVC's class iterator
                _buffer.insert(_buffer.end(), chunk.data(), chunk.data() + *received);
            }
        }

        /// The frames of a server are not masked
        std::optional<ServerFrame> ParseBuffered() {
            if (_buffer.size() < 2) {
                return std::nullopt;
            }
            size_t length = _buffer[1] & 0x7F;
            size_t headerSize = 2;
            if (length == 126 || length == 127) {
                const size_t lengthBytes = (length == 126) ? 2 : 8;
                if (_buffer.size() < 2 + lengthBytes) {
                    return std::nullopt;
                }
                length = 0;
                for (size_t i = 0; i < lengthBytes; ++i) {
                    length = (length << 8) | _buffer[2 + i];
                }
                headerSize += lengthBytes;
            }
            if (_buffer.size() < headerSize + length) {
                return std::nullopt;
            }
            ServerFrame frame;
            frame.opcode = _buffer[0] & 0x0F;
            frame.payload.assign(reinterpret_cast<const char*>(_buffer.data()) + headerSize, length);
            _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(headerSize + length));
            return frame;
        }

        Socket _socket;
        WaitPump _pump;
        eastl::vector<uint8_t> _buffer;
        bool _ended = false;
    };

}  // namespace BECore::Tests
