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

    inline Span<const uint8_t> AsBytes(const String& text) {
        return Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
    }

    inline Span<const uint8_t> AsBytes(const Vector<uint8_t>& bytes) {
        return Span<const uint8_t>(bytes.data(), bytes.size());
    }

    /// The opening handshake of the RFC for a server on @p port; @p host replaces the `Host` header value when it is not empty
    inline String HandshakeRequest(uint16_t port, const String& extraHeaders = "", const String& host = "") {
        const String hostValue = host.empty() ? "127.0.0.1:" + ToString(port) : host;
        return "GET /control HTTP/1.1\r\nHost: " + hostValue + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + String(kClientKey) + "\r\nSec-WebSocket-Version: 13\r\n" +
               extraHeaders + "\r\n";
    }

    /// A frame of the server (not masked)
    struct ServerFrame {
        uint8_t opcode = 0;
        String payload;
        /// The mask bit of the header: a server must never set it (RFC 6455, 5.1)
        bool masked = false;
    };

    inline uint16_t CloseCodeOf(const ServerFrame& frame) {
        return static_cast<uint16_t>((static_cast<uint8_t>(frame.payload[0]) << 8) | static_cast<uint8_t>(frame.payload[1]));
    }

    /// The reason of a close frame (after the two bytes of the code)
    inline String CloseReasonOf(const ServerFrame& frame) {
        return frame.payload.size() > 2 ? frame.payload.substr(2) : String();
    }

    class WebSocketTestClient {
    public:
        /// Connects to 127.0.0.1:@p port (no handshake yet). @p pump runs while the client waits
        static Optional<WebSocketTestClient> Open(uint16_t port, WaitPump pump = {}) {
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

        void Send(Span<const uint8_t> bytes) {
            EXPECT_TRUE(_socket.SendAll(bytes));
        }

        void Send(const String& text) {
            Send(AsBytes(text));
        }

        /// Sends without a test failure when the server has ended the connection (a client that is being dropped); false then
        [[nodiscard]] bool TrySend(Span<const uint8_t> bytes) {
            return _socket.SendAll(bytes);
        }

        void SendText(const String& text) {
            Send(AsBytes(EncodeTextFrame(text, kClientMask)));
        }

        /// The response head of the handshake, and the body when it says Content-Length. Empty when nothing came in time
        String ReadHttpResponse() {
            constexpr StringView kHeadEnd = "\r\n\r\n";
            String response;
            size_t bodyExpected = 0;
            const bool complete = WaitUntil(_pump, [&] {
                FillBuffer();
                const String text(reinterpret_cast<const char*>(_buffer.data()), _buffer.size());
                const size_t headEnd = text.find(kHeadEnd.data(), 0, kHeadEnd.size());
                if (headEnd == String::npos) {
                    return false;
                }
                const size_t lengthAt = text.find("Content-Length: ");
                bodyExpected = (lengthAt == String::npos || lengthAt > headEnd) ? 0 : static_cast<size_t>(std::stoul(text.substr(lengthAt + 16).c_str()));
                if (text.size() < headEnd + kHeadEnd.size() + bodyExpected) {
                    return false;
                }
                response = text.substr(0, headEnd + kHeadEnd.size() + bodyExpected);
                _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(response.size()));
                return true;
            });
            return complete ? response : String();
        }

        /// Performs the handshake of the RFC and returns whether the server switched protocols
        bool Handshake(uint16_t port, const String& extraHeaders = "") {
            Send(HandshakeRequest(port, extraHeaders));
            return StringView(ReadHttpResponse()).starts_with("HTTP/1.1 101 ");
        }

        /// The next complete frame of the server, nullopt when none came yet
        Optional<ServerFrame> TryReadFrame() {
            if (const auto frame = ParseBuffered()) {
                return frame;
            }
            FillBuffer();
            return ParseBuffered();
        }

        /// Waits up to @p limit for the next frame
        Optional<ServerFrame> WaitForFrame(std::chrono::milliseconds limit = kClientWaitLimit) {
            Optional<ServerFrame> frame;
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
            Array<uint8_t, 4096> chunk;
            for (int i = 0; i < kMaxChunksPerFill; ++i) {
                const auto received = _socket.Receive(Span<uint8_t>(chunk.data(), chunk.size()));
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
        Optional<ServerFrame> ParseBuffered() {
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
            frame.masked = (_buffer[1] & 0x80) != 0;
            frame.payload.assign(reinterpret_cast<const char*>(_buffer.data()) + headerSize, length);
            _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(headerSize + length));
            return frame;
        }

        Socket _socket;
        WaitPump _pump;
        Vector<uint8_t> _buffer;
        bool _ended = false;
    };

}  // namespace BECore::Tests
