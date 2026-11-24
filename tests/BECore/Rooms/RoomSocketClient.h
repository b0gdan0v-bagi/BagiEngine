#pragma once

#include <BECore/Rooms/TestClient.h>
#include <BECore/Types/Types.h>
#include <BECore/WebSocketTestClient.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace BECore::Tests {

    /**
     * @brief A client of a RoomWebSocketServer over a real socket: the raw WebSocket client plus the messages of the room protocol.
     *
     * Everything the server sends is parsed and kept in Received(). Next() and WaitFor() read forward from a cursor, so a test can
     * ask for "the next `lobby`" without being bothered by the messages in between.
     */
    class RoomSocketClient {
    public:
        /// Opens a connection and performs the handshake; nullopt (and a test failure) when the server does not switch protocols
        static Optional<RoomSocketClient> Connect(uint16_t port, const String& extraHeaders = "") {
            auto raw = WebSocketTestClient::Open(port);
            if (!raw) {
                return std::nullopt;
            }
            if (!raw->Handshake(port, extraHeaders)) {
                ADD_FAILURE() << "the handshake was not accepted";
                return std::nullopt;
            }
            return RoomSocketClient(std::move(*raw));
        }

        WebSocketTestClient& Raw() {
            return _raw;
        }

        void SendText(const String& text) {
            _raw.SendText(text);
        }

        void Send(const ClientMessage& message) {
            const String text = SerializeClientMessage(message);
            _raw.SendText(text);
        }

        void Create(StringView name, StringView game = "fake") {
            Send(CreateRoomMessage{String(game), String(name), String()});
        }

        void JoinByName(StringView code, StringView name) {
            JoinRoomMessage message;
            message.code = String(code);
            message.name = String(name);
            Send(message);
        }

        void JoinByToken(StringView code, StringView token) {
            JoinRoomMessage message;
            message.code = String(code);
            message.token = String(token);
            Send(message);
        }

        void Start() {
            Send(StartMessage{});
        }

        void Leave() {
            Send(LeaveMessage{});
        }

        /// The next message the server sent that this client has not looked at yet; nullopt when none came in @p limit, or when the
        /// server sent a close frame (then in GetCloseFrame()). A ping on the way is answered with a pong, as a browser does; pongs and
        /// binary frames are counted and skipped
        Optional<ReceivedMessage> Next(std::chrono::milliseconds limit = kClientWaitLimit) {
            if (_cursor < _received.size()) {
                return _received[_cursor++];
            }
            const auto deadline = std::chrono::steady_clock::now() + limit;
            Optional<RawFrame> frame;
            for (;;) {
                const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
                frame = _raw.WaitForFrame(std::max(left, std::chrono::milliseconds{0}));
                if (!frame) {
                    return std::nullopt;
                }
                _maskedFrames += frame->masked ? 1 : 0;
                if (frame->opcode == WebSocketOpcode::kText) {
                    break;
                }
                if (frame->opcode == WebSocketOpcode::kClose) {
                    _closeFrame = *frame;
                    return std::nullopt;
                }
                if (frame->opcode == WebSocketOpcode::kPing) {
                    ++_pings;
                    _raw.Send(AsBytes(EncodeFrame(WebSocketOpcode::kPong, AsBytes(frame->payload), kClientMask)));
                }
                _binaryFrames += frame->opcode == WebSocketOpcode::kBinary ? 1 : 0;
            }
            ReceivedMessage message = ReadReceivedMessage(String(frame->payload.data(), frame->payload.size()));
            _received.push_back(message);
            ++_cursor;
            if (message.type == RoomMessageTypes::kJoined) {
                roomCode = message.GetString("room");
                token = message.GetString("token");
                seat = static_cast<int>(message.GetInt("seat"));
            }
            return message;
        }

        /// Reads forward until a message of @p type arrives (the ones in between stay in Received()); nullopt when none does
        Optional<ReceivedMessage> WaitFor(StringView type, std::chrono::milliseconds limit = kClientWaitLimit) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (std::chrono::steady_clock::now() < deadline) {
                const auto message = Next(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()));
                if (!message) {
                    return std::nullopt;
                }
                if (message->type == type) {
                    return message;
                }
            }
            return std::nullopt;
        }

        /// Reads forward until the server's close frame arrives (the messages before it are kept); nullopt when none does
        Optional<RawFrame> WaitForClose(std::chrono::milliseconds limit = kClientWaitLimit) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!_closeFrame && std::chrono::steady_clock::now() < deadline) {
                if (!Next(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())) && !_closeFrame) {
                    break;
                }
            }
            return _closeFrame;
        }

        /// Every message received so far, in order
        [[nodiscard]] const Vector<ReceivedMessage>& Received() const {
            return _received;
        }

        /// The close frame the server sent, once Next() met it
        [[nodiscard]] const Optional<RawFrame>& GetCloseFrame() const {
            return _closeFrame;
        }

        /// Frames of the server with the mask bit set, and binary frames, seen so far: a server sends neither
        [[nodiscard]] size_t GetMaskedFrameCount() const {
            return _maskedFrames;
        }

        [[nodiscard]] size_t GetBinaryFrameCount() const {
            return _binaryFrames;
        }

        /// The pings of the server that Next() answered
        [[nodiscard]] size_t GetPingCount() const {
            return _pings;
        }

        /// Types of the messages received so far, in order ("joined,lobby")
        [[nodiscard]] String Types() const {
            return TypesOf(_received);
        }

        /// What the client learned from `joined`
        String roomCode;
        String token;
        int seat = -1;

    private:
        explicit RoomSocketClient(WebSocketTestClient raw) : _raw(std::move(raw)) {}

        WebSocketTestClient _raw;
        Vector<ReceivedMessage> _received;
        size_t _cursor = 0;
        Optional<RawFrame> _closeFrame;
        size_t _maskedFrames = 0;
        size_t _binaryFrames = 0;
        size_t _pings = 0;
    };

}  // namespace BECore::Tests
