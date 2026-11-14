#include "FuzzTargets.h"
#include "FuzzWebSocketInput.h"

namespace BECore::Fuzz {

    namespace {

        using WebSocketInput::kMessageLimitCount;
        using WebSocketInput::kMessageLimits;

        constexpr uint8_t kPatternMask = 0x0F;
        constexpr uint8_t kLimitMask = WebSocketInput::kLimitMask;
        constexpr size_t kRepeatedPops = 2;  // how often a parser in the error state is asked again

        /// The key of the frames that are made again for the other role
        constexpr WebSocketMask kReencodeMask = {0x12, 0x34, 0x56, 0x78};

        /// What a parser returned for a whole stream: the messages in order and the first error
        struct Outcome {
            Vector<WebSocketMessage> messages;
            Optional<WebSocketProtocolError> error;
        };

        /// Takes every message that is ready; stops at "more bytes" or at the error (and remembers the first one only)
        void Collect(WebSocketFrameParser& parser, Outcome& outcome) {
            for (;;) {
                auto result = parser.Pop();
                if (!result.has_value()) {
                    if (!outcome.error) {
                        outcome.error = result.error();
                    } else {
                        FUZZ_CHECK(outcome.error->closeCode == result.error().closeCode);
                        FUZZ_CHECK(outcome.error->reason == result.error().reason);
                    }
                    return;
                }
                if (!result->has_value()) {
                    return;
                }
                FUZZ_CHECK(!outcome.error);  // nothing is returned after an error
                outcome.messages.push_back(std::move(**result));
            }
        }

        /// The size of the chunk number @p index of a stream fed with @p pattern: 1 to 128 bytes, the range grows with the pattern
        size_t ChunkSize(uint8_t pattern, size_t index) {
            const size_t range = size_t{1} << (pattern % 8);
            const size_t step = (index * (pattern / 8 + 1) * 7) % range;
            return 1 + step;
        }

        Outcome Parse(Span<const uint8_t> stream, size_t limit, WebSocketPeer sender, uint8_t pattern) {
            WebSocketFrameParser parser(limit, sender);
            Outcome outcome;
            if (pattern == 0) {
                parser.Feed(stream);
                Collect(parser, outcome);
            } else {
                size_t position = 0;
                for (size_t index = 0; position < stream.size(); ++index) {
                    const size_t count = std::min(ChunkSize(pattern, index), stream.size() - position);
                    parser.Feed(stream.subspan(position, count));
                    position += count;
                    Collect(parser, outcome);
                }
            }
            // After an error the parser stays in it, whatever is fed or asked
            if (outcome.error) {
                const uint8_t more[] = {0x81, 0x00};
                parser.Feed(Span<const uint8_t>(more, sizeof(more)));
                for (size_t i = 0; i < kRepeatedPops; ++i) {
                    Collect(parser, outcome);
                }
            }
            return outcome;
        }

        bool SameMessage(const WebSocketMessage& left, const WebSocketMessage& right) {
            return left.kind == right.kind && left.closeCode == right.closeCode && left.payload == right.payload;
        }

        uint8_t OpcodeOf(WebSocketMessageKind kind) {
            switch (kind) {
                case WebSocketMessageKind::Text:
                    return WebSocketOpcode::kText;
                case WebSocketMessageKind::Binary:
                    return WebSocketOpcode::kBinary;
                case WebSocketMessageKind::Ping:
                    return WebSocketOpcode::kPing;
                case WebSocketMessageKind::Pong:
                    return WebSocketOpcode::kPong;
                default:
                    return WebSocketOpcode::kClose;
            }
        }

        /// The messages as the other role would send them: the frames of a client are masked, those of a server are not
        Vector<uint8_t> EncodeForOtherRole(const Vector<WebSocketMessage>& messages, WebSocketPeer sender) {
            const Optional<WebSocketMask> mask = (sender == WebSocketPeer::Client) ? std::nullopt : Optional<WebSocketMask>(kReencodeMask);
            Vector<uint8_t> stream;
            for (const WebSocketMessage& message : messages) {
                Vector<uint8_t> payload;
                if (message.kind == WebSocketMessageKind::Close && message.closeCode != WebSocketCloseCode::kNoStatusReceived) {
                    payload.push_back(static_cast<uint8_t>(message.closeCode >> 8));
                    payload.push_back(static_cast<uint8_t>(message.closeCode & 0xFF));
                }
                payload.insert(payload.end(), message.payload.begin(), message.payload.end());
                const Vector<uint8_t> frame = EncodeFrame(OpcodeOf(message.kind), Span<const uint8_t>(payload.data(), payload.size()), mask);
                stream.insert(stream.end(), frame.begin(), frame.end());
            }
            return stream;
        }

    }  // namespace

    void FuzzWebSocketFrame(const uint8_t* data, size_t size) {
        if (size == 0) {
            return;
        }
        const uint8_t selector = data[0];
        const WebSocketPeer sender = (selector & WebSocketInput::kPeerBit) ? WebSocketPeer::Server : WebSocketPeer::Client;
        const size_t limit = kMessageLimits[((selector >> WebSocketInput::kLimitShift) & kLimitMask) % kMessageLimitCount];
        const uint8_t pattern = (selector >> WebSocketInput::kPatternShift) & kPatternMask;
        const Span<const uint8_t> stream(data + 1, size - 1);

        // The whole stream at once is the reference; any chunking gives the same messages and the same first error
        const Outcome whole = Parse(stream, limit, sender, 0);
        const Outcome chunked = Parse(stream, limit, sender, pattern == 0 ? 1 : pattern);
        FUZZ_CHECK(whole.messages.size() == chunked.messages.size());
        for (size_t i = 0; i < whole.messages.size(); ++i) {
            FUZZ_CHECK(SameMessage(whole.messages[i], chunked.messages[i]));
        }
        FUZZ_CHECK(whole.error.has_value() == chunked.error.has_value());
        if (whole.error) {
            FUZZ_CHECK(whole.error->closeCode == chunked.error->closeCode);
            FUZZ_CHECK(whole.error->reason == chunked.error->reason);
            FUZZ_CHECK(!whole.error->reason.empty());
        }

        for (const WebSocketMessage& message : whole.messages) {
            if (message.kind == WebSocketMessageKind::Text || message.kind == WebSocketMessageKind::Binary) {
                FUZZ_CHECK(message.payload.size() <= limit);
            } else {
                FUZZ_CHECK(message.payload.size() <= kMaxControlPayload);
            }
        }

        // What the parser returned, sent by the other role, is read back as it was
        const WebSocketPeer other = (sender == WebSocketPeer::Client) ? WebSocketPeer::Server : WebSocketPeer::Client;
        const Vector<uint8_t> reencoded = EncodeForOtherRole(whole.messages, sender);
        const Outcome again = Parse(Span<const uint8_t>(reencoded.data(), reencoded.size()), limit, other, 0);
        FUZZ_CHECK(!again.error.has_value());
        FUZZ_CHECK(again.messages.size() == whole.messages.size());
        for (size_t i = 0; i < whole.messages.size(); ++i) {
            FUZZ_CHECK(SameMessage(whole.messages[i], again.messages[i]));
        }
    }

}  // namespace BECore::Fuzz
