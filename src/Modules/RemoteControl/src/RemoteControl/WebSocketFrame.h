#pragma once

#include <BECore/Control/ControlService.h>

namespace BECore {

    /// Opcodes of RFC 6455, section 5.2
    namespace WebSocketOpcode {
        inline constexpr uint8_t kContinuation = 0x0;
        inline constexpr uint8_t kText = 0x1;
        inline constexpr uint8_t kBinary = 0x2;
        inline constexpr uint8_t kClose = 0x8;
        inline constexpr uint8_t kPing = 0x9;
        inline constexpr uint8_t kPong = 0xA;
    }  // namespace WebSocketOpcode

    /// Close codes of RFC 6455, section 7.4.1
    namespace WebSocketCloseCode {
        inline constexpr uint16_t kNormal = 1000;
        inline constexpr uint16_t kGoingAway = 1001;
        inline constexpr uint16_t kProtocolError = 1002;
        inline constexpr uint16_t kUnsupportedData = 1003;
        inline constexpr uint16_t kNoStatusReceived = 1005;  // never sent: the value of a close frame that has no code
        inline constexpr uint16_t kPolicyViolation = 1008;
        inline constexpr uint16_t kMessageTooBig = 1009;
        inline constexpr uint16_t kTryAgainLater = 1013;  // the server is full or busy: connect again later
    }  // namespace WebSocketCloseCode

    /// Longest payload of a control frame (close, ping, pong), RFC 6455 section 5.5
    inline constexpr size_t kMaxControlPayload = 125;

    /// Masking key of a client frame
    using WebSocketMask = std::array<uint8_t, 4>;

    /**
     * @brief Encodes one frame.
     *
     * A server sends its frames unmasked. @p mask is for clients and tests: a frame with a key is masked, as a
     * client's frames have to be. @p fin false starts or continues a fragmented message.
     */
    [[nodiscard]] eastl::vector<uint8_t> EncodeFrame(uint8_t opcode, eastl::span<const uint8_t> payload, std::optional<WebSocketMask> mask = std::nullopt, bool fin = true);

    /// A text frame holding @p text
    [[nodiscard]] eastl::vector<uint8_t> EncodeTextFrame(eastl::string_view text, std::optional<WebSocketMask> mask = std::nullopt);

    /// A close frame with @p code and @p reason (the reason is cut to fit a control frame)
    [[nodiscard]] eastl::vector<uint8_t> EncodeCloseFrame(uint16_t code, eastl::string_view reason = {}, std::optional<WebSocketMask> mask = std::nullopt);

    CORE_ENUM(WebSocketMessageKind, uint8_t, Text, Binary, Ping, Pong, Close)

    /// A complete message of the peer: a text or binary message put together from its fragments, or a control frame
    struct WebSocketMessage {
        WebSocketMessageKind kind = WebSocketMessageKind::Text;
        eastl::vector<uint8_t> payload;                              // Close: the reason, without the code
        uint16_t closeCode = WebSocketCloseCode::kNoStatusReceived;  // Close only

        /// The payload as text (Text, Ping, Pong and the reason of a Close)
        [[nodiscard]] eastl::string_view GetText() const {
            return eastl::string_view(reinterpret_cast<const char*>(payload.data()), payload.size());
        }
    };

    /// Who sends the frames a parser reads: the frames of a client are masked, those of a server are not (RFC 6455, section 5.1)
    CORE_ENUM(WebSocketPeer, uint8_t, Client, Server)

    /// A client broke the protocol: the connection has to be closed with @p closeCode
    struct WebSocketProtocolError {
        uint16_t closeCode = WebSocketCloseCode::kProtocolError;
        eastl::string reason;
    };

    /**
     * @brief Turns the bytes that a client sends into messages (RFC 6455, section 5).
     *
     * Feed() takes bytes as they arrive from the socket, in pieces of any size; Pop() returns the messages that are
     * complete. A client's frames have to be masked, RSV bits are zero, control frames are at most 125 bytes and not
     * fragmented, a text or binary message may be split into fragments with a ping or pong between them, and a
     * message may not exceed the limit: the length in the frame header is checked before the payload is stored, so a
     * client cannot make the server buffer more than the limit.
     *
     * The parser reads the frames of a client by default; WebSocketPeer::Server makes it the parser of a client that
     * reads a server: the frames must then be unmasked (a masked one is a protocol error). Everything else is shared.
     *
     * The first violation is final: every later Pop() returns the same error and the caller closes the connection.
     * The parser does not check that text is UTF-8 (RFC 6455 section 5.6 asks for it): the control API answers text
     * that is not valid JSON with a parse_error, which is enough for a development tool.
     */
    class WebSocketFrameParser {
    public:
        /// @p sender is who sent the bytes that are fed
        explicit WebSocketFrameParser(size_t maxMessageBytes = ControlService::kMaxRequestBytes, WebSocketPeer sender = WebSocketPeer::Client) : _maxMessageBytes(maxMessageBytes), _sender(sender) {}

        void Feed(eastl::span<const uint8_t> bytes);

        /// The next complete message, nullopt when more bytes are needed
        [[nodiscard]] std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> Pop();

    private:
        struct FrameHeader {
            bool fin = false;
            uint8_t opcode = 0;
            uint64_t payloadLength = 0;
            size_t headerSize = 0;  // the bytes before the payload, the masking key included
            WebSocketMask mask{};
        };

        /// Reads the header at the read position: nullopt when it is not complete yet, an error when it is not valid
        [[nodiscard]] std::expected<std::optional<FrameHeader>, WebSocketProtocolError> ReadHeader() const;
        [[nodiscard]] std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> Accept(const FrameHeader& header, eastl::vector<uint8_t> payload);
        [[nodiscard]] std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> Fail(uint16_t closeCode, eastl::string reason);

        size_t _maxMessageBytes;
        WebSocketPeer _sender;
        eastl::vector<uint8_t> _buffer;
        size_t _readPosition = 0;  // what is before it has been consumed; Feed() drops it
        std::optional<WebSocketProtocolError> _error;

        bool _inFragmentedMessage = false;
        WebSocketMessageKind _fragmentKind = WebSocketMessageKind::Text;
        eastl::vector<uint8_t> _fragments;
    };

}  // namespace BECore
