#include "WebSocketFrame.h"

namespace BECore {

    namespace {

        constexpr uint8_t kFinBit = 0x80;
        constexpr uint8_t kReservedBits = 0x70;
        constexpr uint8_t kOpcodeMask = 0x0F;
        constexpr uint8_t kMaskBit = 0x80;
        constexpr uint8_t kLengthMask = 0x7F;
        constexpr uint8_t kLength16Marker = 126;  // the length is in the next 2 bytes
        constexpr uint8_t kLength64Marker = 127;  // the length is in the next 8 bytes
        constexpr size_t kMaskSize = 4;
        constexpr size_t kCloseCodeSize = 2;
        constexpr uint8_t kFirstControlOpcode = 0x8;

        bool IsControlOpcode(uint8_t opcode) {
            return opcode >= kFirstControlOpcode;
        }

        bool IsKnownOpcode(uint8_t opcode) {
            return opcode == WebSocketOpcode::kContinuation || opcode == WebSocketOpcode::kText || opcode == WebSocketOpcode::kBinary || opcode == WebSocketOpcode::kClose ||
                   opcode == WebSocketOpcode::kPing || opcode == WebSocketOpcode::kPong;
        }

        /// The close codes that may appear on the wire (RFC 6455 section 7.4): 1004, 1005, 1006 and 1015 are reserved
        bool IsSendableCloseCode(uint16_t code) {
            return (code >= 1000 && code <= 1003) || (code >= 1007 && code <= 1014) || (code >= 3000 && code <= 4999);
        }

        void AppendBigEndian(eastl::vector<uint8_t>& out, uint64_t value, size_t byteCount) {
            for (size_t i = 0; i < byteCount; ++i) {
                out.push_back(static_cast<uint8_t>(value >> (8 * (byteCount - 1 - i))));
            }
        }

        uint64_t ReadBigEndian(const uint8_t* bytes, size_t byteCount) {
            uint64_t value = 0;
            for (size_t i = 0; i < byteCount; ++i) {
                value = (value << 8) | bytes[i];
            }
            return value;
        }

        std::unexpected<WebSocketProtocolError> Violation(uint16_t closeCode, eastl::string reason) {
            return std::unexpected(WebSocketProtocolError{closeCode, std::move(reason)});
        }

    }  // namespace

    eastl::vector<uint8_t> EncodeFrame(uint8_t opcode, eastl::span<const uint8_t> payload, std::optional<WebSocketMask> mask, bool fin) {
        eastl::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>((fin ? kFinBit : 0) | (opcode & kOpcodeMask)));

        const uint8_t maskBit = mask ? kMaskBit : 0;
        if (payload.size() < kLength16Marker) {
            frame.push_back(static_cast<uint8_t>(maskBit | payload.size()));
        } else if (payload.size() <= std::numeric_limits<uint16_t>::max()) {
            frame.push_back(static_cast<uint8_t>(maskBit | kLength16Marker));
            AppendBigEndian(frame, payload.size(), 2);
        } else {
            frame.push_back(static_cast<uint8_t>(maskBit | kLength64Marker));
            AppendBigEndian(frame, payload.size(), 8);
        }

        if (mask) {
            // Pointers, not std::array iterators: EASTL's insert does not take MSVC's class iterator
            frame.insert(frame.end(), mask->data(), mask->data() + mask->size());
        }
        for (size_t i = 0; i < payload.size(); ++i) {
            frame.push_back(mask ? static_cast<uint8_t>(payload[i] ^ (*mask)[i % kMaskSize]) : payload[i]);
        }
        return frame;
    }

    eastl::vector<uint8_t> EncodeTextFrame(eastl::string_view text, std::optional<WebSocketMask> mask) {
        return EncodeFrame(WebSocketOpcode::kText, eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()), mask);
    }

    eastl::vector<uint8_t> EncodeCloseFrame(uint16_t code, eastl::string_view reason, std::optional<WebSocketMask> mask) {
        eastl::vector<uint8_t> payload;
        AppendBigEndian(payload, code, kCloseCodeSize);
        const size_t reasonSize = std::min(reason.size(), kMaxControlPayload - kCloseCodeSize);
        payload.insert(payload.end(), reinterpret_cast<const uint8_t*>(reason.data()), reinterpret_cast<const uint8_t*>(reason.data()) + reasonSize);
        return EncodeFrame(WebSocketOpcode::kClose, eastl::span<const uint8_t>(payload.data(), payload.size()), mask);
    }

    void WebSocketFrameParser::Feed(eastl::span<const uint8_t> bytes) {
        if (_error) {
            return;  // the connection is being closed: what still arrives is not kept
        }
        if (_readPosition > 0) {
            _buffer.erase(_buffer.begin(), _buffer.begin() + static_cast<ptrdiff_t>(_readPosition));
            _readPosition = 0;
        }
        _buffer.insert(_buffer.end(), bytes.begin(), bytes.end());
    }

    std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> WebSocketFrameParser::Fail(uint16_t closeCode, eastl::string reason) {
        _error = WebSocketProtocolError{closeCode, std::move(reason)};
        _buffer.clear();
        _buffer.set_capacity(0);
        _readPosition = 0;
        _fragments.clear();
        _fragments.set_capacity(0);
        _inFragmentedMessage = false;
        return std::unexpected(*_error);
    }

    std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> WebSocketFrameParser::Pop() {
        if (_error) {
            return std::unexpected(*_error);
        }

        // A frame that is not the last fragment of a message produces no message: go on with the next buffered frame
        for (;;) {
            const auto header = ReadHeader();
            if (!header) {
                return Fail(header.error().closeCode, header.error().reason);
            }
            if (!header->has_value()) {
                return std::optional<WebSocketMessage>();
            }

            const FrameHeader& frame = **header;
            const size_t frameSize = frame.headerSize + static_cast<size_t>(frame.payloadLength);
            if (_buffer.size() - _readPosition < frameSize) {
                return std::optional<WebSocketMessage>();
            }

            eastl::vector<uint8_t> payload(static_cast<size_t>(frame.payloadLength));
            const uint8_t* source = _buffer.data() + _readPosition + frame.headerSize;
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] = source[i] ^ frame.mask[i % kMaskSize];
            }
            _readPosition += frameSize;

            auto accepted = Accept(frame, std::move(payload));
            if (!accepted || accepted->has_value()) {
                return accepted;
            }
        }
    }

    std::expected<std::optional<WebSocketFrameParser::FrameHeader>, WebSocketProtocolError> WebSocketFrameParser::ReadHeader() const {
        const uint8_t* data = _buffer.data() + _readPosition;
        const size_t available = _buffer.size() - _readPosition;
        if (available < 2) {
            return std::optional<FrameHeader>();
        }

        FrameHeader header;
        header.fin = (data[0] & kFinBit) != 0;
        header.opcode = data[0] & kOpcodeMask;
        if ((data[0] & kReservedBits) != 0) {
            return Violation(WebSocketCloseCode::kProtocolError, "reserved bits are set");
        }
        if (!IsKnownOpcode(header.opcode)) {
            return Violation(WebSocketCloseCode::kProtocolError, Format("unknown opcode {}", static_cast<unsigned>(header.opcode)));
        }
        const bool isMasked = (data[1] & kMaskBit) != 0;
        if (_sender == WebSocketPeer::Client && !isMasked) {
            return Violation(WebSocketCloseCode::kProtocolError, "the frames of a client have to be masked");
        }
        if (_sender == WebSocketPeer::Server && isMasked) {
            return Violation(WebSocketCloseCode::kProtocolError, "the frames of a server must not be masked");
        }

        const uint8_t lengthCode = data[1] & kLengthMask;
        const bool isControl = IsControlOpcode(header.opcode);
        if (isControl && !header.fin) {
            return Violation(WebSocketCloseCode::kProtocolError, "a control frame cannot be fragmented");
        }
        if (isControl && lengthCode > kMaxControlPayload) {
            return Violation(WebSocketCloseCode::kProtocolError, "a control frame is longer than 125 bytes");
        }

        size_t position = 2;
        header.payloadLength = lengthCode;
        if (lengthCode == kLength16Marker || lengthCode == kLength64Marker) {
            const size_t lengthBytes = (lengthCode == kLength16Marker) ? 2 : 8;
            if (available < position + lengthBytes) {
                return std::optional<FrameHeader>();
            }
            header.payloadLength = ReadBigEndian(data + position, lengthBytes);
            position += lengthBytes;
        }

        // The limit is checked on the announced length, before any of the payload is stored
        if (!isControl && (header.payloadLength > _maxMessageBytes || _fragments.size() + header.payloadLength > _maxMessageBytes)) {
            return Violation(WebSocketCloseCode::kMessageTooBig, Format("a message is longer than {} bytes", _maxMessageBytes));
        }

        if (isMasked) {
            if (available < position + kMaskSize) {
                return std::optional<FrameHeader>();
            }
            std::memcpy(header.mask.data(), data + position, kMaskSize);
            position += kMaskSize;
        }  // an unmasked frame keeps the all-zero key, which leaves the payload as it is
        header.headerSize = position;
        return std::optional<FrameHeader>(header);
    }

    std::expected<std::optional<WebSocketMessage>, WebSocketProtocolError> WebSocketFrameParser::Accept(const FrameHeader& header, eastl::vector<uint8_t> payload) {
        WebSocketMessage message;
        switch (header.opcode) {
            case WebSocketOpcode::kContinuation:
                if (!_inFragmentedMessage) {
                    return Fail(WebSocketCloseCode::kProtocolError, "a continuation frame has no message to continue");
                }
                _fragments.insert(_fragments.end(), payload.begin(), payload.end());
                if (!header.fin) {
                    return std::optional<WebSocketMessage>();
                }
                message.kind = _fragmentKind;
                message.payload = std::move(_fragments);
                _fragments.clear();
                _inFragmentedMessage = false;
                return std::optional<WebSocketMessage>(std::move(message));

            case WebSocketOpcode::kText:
            case WebSocketOpcode::kBinary:
                if (_inFragmentedMessage) {
                    return Fail(WebSocketCloseCode::kProtocolError, "a new message starts before the previous one has ended");
                }
                message.kind = (header.opcode == WebSocketOpcode::kText) ? WebSocketMessageKind::Text : WebSocketMessageKind::Binary;
                if (!header.fin) {
                    _inFragmentedMessage = true;
                    _fragmentKind = message.kind;
                    _fragments = std::move(payload);
                    return std::optional<WebSocketMessage>();
                }
                message.payload = std::move(payload);
                return std::optional<WebSocketMessage>(std::move(message));

            case WebSocketOpcode::kPing:
            case WebSocketOpcode::kPong:
                message.kind = (header.opcode == WebSocketOpcode::kPing) ? WebSocketMessageKind::Ping : WebSocketMessageKind::Pong;
                message.payload = std::move(payload);
                return std::optional<WebSocketMessage>(std::move(message));

            default:  // WebSocketOpcode::kClose: ReadHeader() has let no other opcode through
                message.kind = WebSocketMessageKind::Close;
                if (payload.size() == 1) {
                    return Fail(WebSocketCloseCode::kProtocolError, "a close frame cannot have a payload of one byte");
                }
                if (payload.size() >= kCloseCodeSize) {
                    const auto code = static_cast<uint16_t>(ReadBigEndian(payload.data(), kCloseCodeSize));
                    if (!IsSendableCloseCode(code)) {
                        return Fail(WebSocketCloseCode::kProtocolError, Format("the close code {} is not valid", code));
                    }
                    message.closeCode = code;
                    message.payload.assign(payload.begin() + kCloseCodeSize, payload.end());
                }
                return std::optional<WebSocketMessage>(std::move(message));
        }
    }

}  // namespace BECore
