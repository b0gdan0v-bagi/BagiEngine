#pragma once

#include <BECore/Utils/FunctionRef.h>
#include <BECore/Utils/MoveOnlyFunction.h>
#include <BECore/Utils/StringUtils.h>
#include <Common/Wait.h>
#include <RemoteControl/Socket.h>
#include <RemoteControl/WebSocketFrame.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    /// A raw WebSocket client for the tests of the servers (WebSocketServerTest, WebSocketListenerTest, the Rooms socket tests):
    /// the handshake and the frames are written by hand, so a server is tested against the bytes of the RFC.

    inline constexpr WebSocketMask kClientMask = {0x12, 0x34, 0x56, 0x78};
    inline constexpr const char* kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";
    inline constexpr std::chrono::seconds kClientWaitLimit = kWaitLimit;
    /// Tries of a client that waits for a free connection slot, 10 ms apart: the accept thread may be starved on a loaded machine
    inline constexpr int kSlotRetryAttempts = 3000;

    /// Something a test thread does while it waits (the server tests drain the control service); may be empty. A client owns its pump:
    /// a test that gives one pump to several clients passes the lambda itself each time, not a `WaitPump` it made once
    using WaitPump = MoveOnlyFunction<void()>;

    /// Calls @p pump and @p done every millisecond until @p done is true or @p limit has passed
    template <typename Predicate>
    bool WaitUntil(const WaitPump& pump, Predicate done, std::chrono::milliseconds limit = kClientWaitLimit) {
        return WaitUntil(
            [&] {
                if (pump) {
                    pump();
                }
                return done();
            },
            limit);
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

    /// A frame as it came off the wire, the payload unmasked. A server's frame must not be masked (RFC 6455, 5.1), a client's must
    struct RawFrame {
        uint8_t opcode = 0;
        String payload;
        /// The mask bit of the header
        bool masked = false;
        /// The masking key when masked
        WebSocketMask key{};
    };

    inline uint16_t CloseCodeOf(const RawFrame& frame) {
        return static_cast<uint16_t>((static_cast<uint8_t>(frame.payload[0]) << 8) | static_cast<uint8_t>(frame.payload[1]));
    }

    /// The reason of a close frame (after the two bytes of the code)
    inline String CloseReasonOf(const RawFrame& frame) {
        return frame.payload.size() > 2 ? frame.payload.substr(2) : String();
    }

    /**
     * @brief The receiving half of a hand-written WebSocket peer: what a socket delivered, and the frames parsed from it.
     *
     * The raw client below reads a server's frames with it, a scripted server (WebSocketClientTest) a client's; both also read an
     * HTTP head from Buffered() before the first frame.
     */
    class RawFrameReader {
    public:
        /// Changes the bytes as they arrive (a fake TLS layer); may be empty
        using Filter = FunctionRef<void(uint8_t* data, size_t size)>;

        /// Reads everything that has arrived (up to kMaxChunksPerFill chunks); a loop, because WaitUntil sleeps 1 ms between its
        /// calls, which is 15 ms on Windows, and a megabyte in chunks of 4 KiB would take seconds
        void Fill(Socket& socket, Optional<Filter> filter = std::nullopt) {
            Array<uint8_t, 4096> chunk;
            for (int i = 0; i < kMaxChunksPerFill; ++i) {
                const auto received = socket.Receive(Span<uint8_t>(chunk.data(), chunk.size()));
                if (!received) {
                    if (received.error() != SocketError::TimedOut) {
                        _ended = true;
                    }
                    return;
                }
                if (filter) {
                    (*filter)(chunk.data(), *received);
                }
                _buffer.append(reinterpret_cast<const char*>(chunk.data()), *received);
            }
        }

        /// The next complete frame of the buffer (unmasked when the mask bit is set), nullopt when it is not all there yet
        Optional<RawFrame> Parse() {
            if (_buffer.size() < 2) {
                return std::nullopt;
            }
            const auto byteAt = [this](size_t index) { return static_cast<uint8_t>(_buffer[index]); };
            RawFrame frame;
            frame.opcode = byteAt(0) & 0x0F;
            frame.masked = (byteAt(1) & 0x80) != 0;
            size_t length = byteAt(1) & 0x7F;
            size_t offset = 2;
            if (length == 126 || length == 127) {
                const size_t lengthBytes = length == 126 ? 2 : 8;
                if (_buffer.size() < offset + lengthBytes) {
                    return std::nullopt;
                }
                length = 0;
                for (size_t i = 0; i < lengthBytes; ++i) {
                    length = (length << 8) | byteAt(offset + i);
                }
                offset += lengthBytes;
            }
            if (frame.masked) {
                if (_buffer.size() < offset + frame.key.size()) {
                    return std::nullopt;
                }
                for (size_t i = 0; i < frame.key.size(); ++i) {
                    frame.key[i] = byteAt(offset + i);
                }
                offset += frame.key.size();
            }
            if (_buffer.size() < offset + length) {
                return std::nullopt;
            }
            frame.payload = _buffer.substr(offset, length);
            if (frame.masked) {
                for (size_t i = 0; i < frame.payload.size(); ++i) {
                    frame.payload[i] = static_cast<char>(static_cast<uint8_t>(frame.payload[i]) ^ frame.key[i % frame.key.size()]);
                }
            }
            _buffer.erase(0, offset + length);
            return frame;
        }

        /// What arrived and was not parsed yet
        [[nodiscard]] String& Buffered() {
            return _buffer;
        }

        /// The peer ended the connection
        [[nodiscard]] bool HasEnded() const {
            return _ended;
        }

    private:
        static constexpr int kMaxChunksPerFill = 1024;

        String _buffer;
        bool _ended = false;
    };

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
                _reader.Fill(_socket);
                String& text = _reader.Buffered();
                const size_t headEnd = text.find(kHeadEnd.data(), 0, kHeadEnd.size());
                if (headEnd == String::npos) {
                    return false;
                }
                const size_t lengthAt = text.find("Content-Length: ");
                if (lengthAt == String::npos || lengthAt > headEnd) {
                    bodyExpected = 0;
                } else {
                    const StringView value = StringView(text).substr(lengthAt + 16);
                    bodyExpected = StringUtils::ParseNumber<size_t>(value.substr(0, value.find('\r'))).value_or(0);
                }
                if (text.size() < headEnd + kHeadEnd.size() + bodyExpected) {
                    return false;
                }
                response = text.substr(0, headEnd + kHeadEnd.size() + bodyExpected);
                text.erase(0, response.size());
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
        Optional<RawFrame> TryReadFrame() {
            if (auto frame = _reader.Parse()) {
                return frame;
            }
            _reader.Fill(_socket);
            return _reader.Parse();
        }

        /// Waits up to @p limit for the next frame
        Optional<RawFrame> WaitForFrame(std::chrono::milliseconds limit = kClientWaitLimit) {
            Optional<RawFrame> frame;
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
                _reader.Fill(_socket);
                return _reader.HasEnded();
            });
        }

        /// Ends the connection from the client's side at once, as a client that vanished
        void Drop() {
            _socket.ShutdownBoth();
            _socket.Close();
        }

    private:
        WebSocketTestClient() = default;

        Socket _socket;
        WaitPump _pump;
        RawFrameReader _reader;
    };

}  // namespace BECore::Tests
