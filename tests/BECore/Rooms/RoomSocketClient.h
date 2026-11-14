#pragma once

#include "../WebSocketTestClient.h"
#include "TestClient.h"

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
        static Optional<RoomSocketClient> Connect(uint16_t port, const std::string& extraHeaders = "") {
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

        void SendText(const std::string& text) {
            _raw.SendText(text);
        }

        void Send(const ClientMessage& message) {
            const eastl::string text = SerializeClientMessage(message);
            _raw.SendText(std::string(text.c_str(), text.size()));
        }

        void Create(eastl::string_view name, eastl::string_view game = "fake") {
            Send(CreateRoomMessage{eastl::string(game), eastl::string(name), Json::object()});
        }

        void JoinByName(eastl::string_view code, eastl::string_view name) {
            JoinRoomMessage message;
            message.code = eastl::string(code);
            message.name = eastl::string(name);
            Send(message);
        }

        void JoinByToken(eastl::string_view code, eastl::string_view token) {
            JoinRoomMessage message;
            message.code = eastl::string(code);
            message.token = eastl::string(token);
            Send(message);
        }

        void Start() {
            Send(StartMessage{});
        }

        void Leave() {
            Send(LeaveMessage{});
        }

        /// The next message the server sent that this client has not looked at yet; nullopt when none came in @p limit, or when the
        /// server sent something that is not a text frame (a close frame is then in GetCloseFrame())
        Optional<ReceivedMessage> Next(std::chrono::milliseconds limit = kClientWaitLimit) {
            if (_cursor < _received.size()) {
                return _received[_cursor++];
            }
            const auto frame = _raw.WaitForFrame(limit);
            if (!frame) {
                return std::nullopt;
            }
            _maskedFrames += frame->masked ? 1 : 0;
            _binaryFrames += frame->opcode == WebSocketOpcode::kBinary ? 1 : 0;
            if (frame->opcode != WebSocketOpcode::kText) {
                _closeFrame = *frame;
                return std::nullopt;
            }
            ReceivedMessage message;
            message.text = eastl::string(frame->payload.data(), frame->payload.size());
            auto parsed = ParseJson(message.text);
            EXPECT_TRUE(parsed.has_value()) << "the server sent text that is not JSON: " << frame->payload;
            if (parsed) {
                message.json = std::move(*parsed);
                message.type = GetJsonString(message.json, "type").value_or(eastl::string());
            }
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
        Optional<ReceivedMessage> WaitFor(eastl::string_view type, std::chrono::milliseconds limit = kClientWaitLimit) {
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
        Optional<ServerFrame> WaitForClose(std::chrono::milliseconds limit = kClientWaitLimit) {
            const auto deadline = std::chrono::steady_clock::now() + limit;
            while (!_closeFrame && std::chrono::steady_clock::now() < deadline) {
                if (!Next(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now())) && !_closeFrame) {
                    break;
                }
            }
            return _closeFrame;
        }

        /// Every message received so far, in order
        [[nodiscard]] const std::vector<ReceivedMessage>& Received() const {
            return _received;
        }

        /// The close frame the server sent, once Next() met it
        [[nodiscard]] const Optional<ServerFrame>& GetCloseFrame() const {
            return _closeFrame;
        }

        /// Frames of the server with the mask bit set, and binary frames, seen so far: a server sends neither
        [[nodiscard]] size_t GetMaskedFrameCount() const {
            return _maskedFrames;
        }

        [[nodiscard]] size_t GetBinaryFrameCount() const {
            return _binaryFrames;
        }

        /// Types of the messages received so far, in order ("joined,lobby")
        [[nodiscard]] std::string Types() const {
            std::string result;
            for (const ReceivedMessage& message : _received) {
                result += (result.empty() ? "" : ",") + std::string(message.type.c_str(), message.type.size());
            }
            return result;
        }

        /// What the client learned from `joined`
        eastl::string roomCode;
        eastl::string token;
        int seat = -1;

    private:
        explicit RoomSocketClient(WebSocketTestClient raw) : _raw(std::move(raw)) {}

        WebSocketTestClient _raw;
        std::vector<ReceivedMessage> _received;
        size_t _cursor = 0;
        Optional<ServerFrame> _closeFrame;
        size_t _maskedFrames = 0;
        size_t _binaryFrames = 0;
    };

}  // namespace BECore::Tests
