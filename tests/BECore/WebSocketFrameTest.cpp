#include <RemoteControl/WebSocketFrame.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr WebSocketMask kMask = {0x37, 0xFA, 0x21, 0x3D};  // the key of the examples in RFC 6455, section 5.7

        Vector<uint8_t> Bytes(std::initializer_list<int> values) {
            Vector<uint8_t> bytes;
            for (const int value : values) {
                bytes.push_back(static_cast<uint8_t>(value));
            }
            return bytes;
        }

        Span<const uint8_t> AsSpan(const Vector<uint8_t>& bytes) {
            return Span<const uint8_t>(bytes.data(), bytes.size());
        }

        // std: meets std::string until the std::string of this file moves to String
        Span<const uint8_t> TextBytes(std::string_view text) {
            return Span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        std::string TextOf(const WebSocketMessage& message) {
            return std::string(message.GetText().data(), message.GetText().size());
        }

        using PopResult = std::expected<Optional<WebSocketMessage>, WebSocketProtocolError>;

        /// The next message, which the test expects to be there
        WebSocketMessage PopMessage(WebSocketFrameParser& parser) {
            PopResult result = parser.Pop();
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().reason.c_str());
            if (!result.has_value() || !result->has_value()) {
                ADD_FAILURE() << "no message was ready";
                return {};
            }
            return std::move(**result);
        }

        void ExpectNothingReady(WebSocketFrameParser& parser) {
            const PopResult result = parser.Pop();
            ASSERT_TRUE(result.has_value()) << result.error().reason.c_str();
            EXPECT_FALSE(result->has_value());
        }

        void ExpectViolation(WebSocketFrameParser& parser, uint16_t closeCode) {
            const PopResult result = parser.Pop();
            ASSERT_FALSE(result.has_value()) << "the frame was accepted";
            EXPECT_EQ(result.error().closeCode, closeCode) << result.error().reason.c_str();
            EXPECT_FALSE(result.error().reason.empty());
        }

        /// The frames of @p frames put into a fresh parser in one piece
        void ExpectViolationOf(const Vector<uint8_t>& frames, uint16_t closeCode, size_t maxMessageBytes = ControlService::kMaxRequestBytes) {
            WebSocketFrameParser parser(maxMessageBytes);
            parser.Feed(AsSpan(frames));
            ExpectViolation(parser, closeCode);
        }

    }  // namespace

    TEST(WebSocketFrameTest, MaskedHelloOfRfc6455) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(Bytes({0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58})));
        const WebSocketMessage message = PopMessage(parser);
        EXPECT_EQ(message.kind, WebSocketMessageKind::Text);
        EXPECT_EQ(TextOf(message), "Hello");
        ExpectNothingReady(parser);
    }

    TEST(WebSocketFrameTest, EncodesTheExamplesOfRfc6455) {
        EXPECT_EQ(EncodeTextFrame("Hello"), Bytes({0x81, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F}));
        EXPECT_EQ(EncodeTextFrame("Hello", kMask), Bytes({0x81, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58}));
        EXPECT_EQ(EncodeFrame(WebSocketOpcode::kPing, TextBytes("Hello")), Bytes({0x89, 0x05, 0x48, 0x65, 0x6C, 0x6C, 0x6F}));
        EXPECT_EQ(EncodeFrame(WebSocketOpcode::kPong, TextBytes("Hello"), kMask), Bytes({0x8A, 0x85, 0x37, 0xFA, 0x21, 0x3D, 0x7F, 0x9F, 0x4D, 0x51, 0x58}));
    }

    TEST(WebSocketFrameTest, EncodesTheLengthInTheShortestForm) {
        const std::string payload125(125, 'a');
        const std::string payload126(126, 'a');
        const std::string payload65535(65535, 'a');
        const std::string payload65536(65536, 'a');

        const auto frame125 = EncodeTextFrame(FromStd(payload125));
        EXPECT_EQ(frame125.size(), 2u + 125);
        EXPECT_EQ(frame125[1], 125);

        const auto frame126 = EncodeTextFrame(FromStd(payload126));
        EXPECT_EQ(frame126.size(), 4u + 126);
        EXPECT_EQ(frame126[1], 126);
        EXPECT_EQ(frame126[2], 0x00);
        EXPECT_EQ(frame126[3], 126);

        const auto frame65535 = EncodeTextFrame(FromStd(payload65535));
        EXPECT_EQ(frame65535.size(), 4u + 65535);
        EXPECT_EQ(frame65535[1], 126);
        EXPECT_EQ(frame65535[2], 0xFF);
        EXPECT_EQ(frame65535[3], 0xFF);

        const auto frame65536 = EncodeTextFrame(FromStd(payload65536));
        EXPECT_EQ(frame65536.size(), 10u + 65536);
        EXPECT_EQ(frame65536[1], 127);
        EXPECT_EQ(frame65536[7], 0x01);
        EXPECT_EQ(frame65536[8], 0x00);
        EXPECT_EQ(frame65536[9], 0x00);
    }

    TEST(WebSocketFrameTest, MessagesOfEveryLengthFormRoundTrip) {
        for (const size_t length : {0u, 1u, 125u, 126u, 127u, 65535u, 65536u, 100000u}) {
            const std::string payload(length, 'x');
            WebSocketFrameParser parser;
            parser.Feed(AsSpan(EncodeTextFrame(FromStd(payload), kMask)));
            const WebSocketMessage message = PopMessage(parser);
            EXPECT_EQ(message.kind, WebSocketMessageKind::Text) << length;
            EXPECT_EQ(TextOf(message), payload) << length;
        }
    }

    TEST(WebSocketFrameTest, TheMaskIsAppliedByTheBytePosition) {
        const std::string payload = "The quick brown fox jumps over the lazy dog";
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeTextFrame(FromStd(payload), WebSocketMask{0x01, 0x02, 0x03, 0x04})));
        EXPECT_EQ(TextOf(PopMessage(parser)), payload);
    }

    TEST(WebSocketFrameTest, BytesMayArriveOneAtATime) {
        const Vector<uint8_t> frame = EncodeTextFrame("{\"id\":1,\"method\":\"ping\"}", kMask);
        WebSocketFrameParser parser;
        for (size_t i = 0; i + 1 < frame.size(); ++i) {
            parser.Feed(Span<const uint8_t>(&frame[i], 1));
            ExpectNothingReady(parser);
        }
        parser.Feed(Span<const uint8_t>(&frame.back(), 1));
        EXPECT_EQ(TextOf(PopMessage(parser)), "{\"id\":1,\"method\":\"ping\"}");
    }

    TEST(WebSocketFrameTest, SeveralFramesInOnePieceComeOutInOrder) {
        Vector<uint8_t> bytes = EncodeTextFrame("one", kMask);
        const Vector<uint8_t> second = EncodeTextFrame("two", kMask);
        const Vector<uint8_t> third = EncodeTextFrame("three", kMask);
        bytes.insert(bytes.end(), second.begin(), second.end());
        bytes.insert(bytes.end(), third.begin(), third.end());

        WebSocketFrameParser parser;
        parser.Feed(AsSpan(bytes));
        EXPECT_EQ(TextOf(PopMessage(parser)), "one");
        EXPECT_EQ(TextOf(PopMessage(parser)), "two");
        EXPECT_EQ(TextOf(PopMessage(parser)), "three");
        ExpectNothingReady(parser);
    }

    TEST(WebSocketFrameTest, ParserKeepsWorkingAfterTheBufferWasDrained) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeTextFrame("first", kMask)));
        EXPECT_EQ(TextOf(PopMessage(parser)), "first");
        parser.Feed(AsSpan(EncodeTextFrame("second", kMask)));
        EXPECT_EQ(TextOf(PopMessage(parser)), "second");
    }

    TEST(WebSocketFrameTest, FragmentsArePutTogether) {
        Vector<uint8_t> bytes = EncodeFrame(WebSocketOpcode::kText, TextBytes("Hel"), kMask, /*fin=*/false);
        const Vector<uint8_t> last = EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("lo"), kMask, /*fin=*/true);
        bytes.insert(bytes.end(), last.begin(), last.end());

        WebSocketFrameParser parser;
        parser.Feed(AsSpan(bytes));
        const WebSocketMessage message = PopMessage(parser);
        EXPECT_EQ(message.kind, WebSocketMessageKind::Text);
        EXPECT_EQ(TextOf(message), "Hello");
        ExpectNothingReady(parser);
    }

    TEST(WebSocketFrameTest, FragmentWithoutTheEndWaits) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kText, TextBytes("Hel"), kMask, /*fin=*/false)));
        ExpectNothingReady(parser);
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("l"), kMask, /*fin=*/false)));
        ExpectNothingReady(parser);
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("o"), kMask, /*fin=*/true)));
        EXPECT_EQ(TextOf(PopMessage(parser)), "Hello");
    }

    TEST(WebSocketFrameTest, PingBetweenFragmentsIsAnsweredFirst) {
        Vector<uint8_t> bytes = EncodeFrame(WebSocketOpcode::kText, TextBytes("Hel"), kMask, /*fin=*/false);
        const Vector<uint8_t> ping = EncodeFrame(WebSocketOpcode::kPing, TextBytes("beat"), kMask);
        const Vector<uint8_t> last = EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("lo"), kMask);
        bytes.insert(bytes.end(), ping.begin(), ping.end());
        bytes.insert(bytes.end(), last.begin(), last.end());

        WebSocketFrameParser parser;
        parser.Feed(AsSpan(bytes));
        const WebSocketMessage first = PopMessage(parser);
        EXPECT_EQ(first.kind, WebSocketMessageKind::Ping);
        EXPECT_EQ(TextOf(first), "beat");
        const WebSocketMessage second = PopMessage(parser);
        EXPECT_EQ(second.kind, WebSocketMessageKind::Text);
        EXPECT_EQ(TextOf(second), "Hello");
    }

    TEST(WebSocketFrameTest, PingAndPongKeepTheirPayload) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes("a"), kMask)));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPong, TextBytes("b"), kMask)));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, {}, kMask)));
        const WebSocketMessage ping = PopMessage(parser);
        EXPECT_EQ(ping.kind, WebSocketMessageKind::Ping);
        EXPECT_EQ(TextOf(ping), "a");
        const WebSocketMessage pong = PopMessage(parser);
        EXPECT_EQ(pong.kind, WebSocketMessageKind::Pong);
        EXPECT_EQ(TextOf(pong), "b");
        const WebSocketMessage emptyPing = PopMessage(parser);
        EXPECT_EQ(emptyPing.kind, WebSocketMessageKind::Ping);
        EXPECT_TRUE(emptyPing.payload.empty());
    }

    TEST(WebSocketFrameTest, BinaryMessageIsReportedAsBinary) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kBinary, AsSpan(Bytes({1, 2, 3})), kMask)));
        const WebSocketMessage message = PopMessage(parser);
        EXPECT_EQ(message.kind, WebSocketMessageKind::Binary);
        EXPECT_EQ(message.payload, Bytes({1, 2, 3}));
    }

    TEST(WebSocketFrameTest, CloseFrameCarriesCodeAndReason) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeCloseFrame(WebSocketCloseCode::kNormal, "bye", kMask)));
        const WebSocketMessage message = PopMessage(parser);
        EXPECT_EQ(message.kind, WebSocketMessageKind::Close);
        EXPECT_EQ(message.closeCode, WebSocketCloseCode::kNormal);
        EXPECT_EQ(TextOf(message), "bye");
    }

    TEST(WebSocketFrameTest, CloseFrameWithoutPayloadHasNoStatus) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kClose, {}, kMask)));
        const WebSocketMessage message = PopMessage(parser);
        EXPECT_EQ(message.kind, WebSocketMessageKind::Close);
        EXPECT_EQ(message.closeCode, WebSocketCloseCode::kNoStatusReceived);
    }

    TEST(WebSocketFrameTest, CloseFrameWithAnInvalidCodeIsAProtocolError) {
        ExpectViolationOf(EncodeFrame(WebSocketOpcode::kClose, AsSpan(Bytes({0x42})), kMask), WebSocketCloseCode::kProtocolError);  // one byte
        for (const uint16_t code : {uint16_t{0}, uint16_t{999}, uint16_t{1004}, uint16_t{1005}, uint16_t{1006}, uint16_t{1015}, uint16_t{2999}, uint16_t{5000}}) {
            SCOPED_TRACE(code);
            ExpectViolationOf(EncodeCloseFrame(code, {}, kMask), WebSocketCloseCode::kProtocolError);
        }
        for (const uint16_t code : {uint16_t{1000}, uint16_t{1003}, uint16_t{1007}, uint16_t{1011}, uint16_t{3000}, uint16_t{4999}}) {
            SCOPED_TRACE(code);
            WebSocketFrameParser parser;
            parser.Feed(AsSpan(EncodeCloseFrame(code, {}, kMask)));
            EXPECT_EQ(PopMessage(parser).closeCode, code);
        }
    }

    TEST(WebSocketFrameTest, EncodedCloseFrameCutsALongReason) {
        const std::string reason(300, 'r');
        const Vector<uint8_t> frame = EncodeCloseFrame(WebSocketCloseCode::kGoingAway, FromStd(reason));
        EXPECT_EQ(frame[0], 0x88);
        EXPECT_EQ(frame[1], kMaxControlPayload);
        EXPECT_EQ(frame[2], 0x03);
        EXPECT_EQ(frame[3], 0xE9);  // 1001
        EXPECT_EQ(frame.size(), 2u + kMaxControlPayload);
    }

    TEST(WebSocketFrameTest, UnmaskedClientFrameIsAProtocolError) {
        ExpectViolationOf(EncodeTextFrame("Hello"), WebSocketCloseCode::kProtocolError);
    }

    TEST(WebSocketFrameTest, ReservedBitsAreAProtocolError) {
        for (const int bit : {0x40, 0x20, 0x10}) {
            Vector<uint8_t> frame = EncodeTextFrame("Hello", kMask);
            frame[0] = static_cast<uint8_t>(frame[0] | bit);
            SCOPED_TRACE(bit);
            ExpectViolationOf(frame, WebSocketCloseCode::kProtocolError);
        }
    }

    TEST(WebSocketFrameTest, UnknownOpcodesAreAProtocolError) {
        for (const uint8_t opcode : {uint8_t{0x3}, uint8_t{0x7}, uint8_t{0xB}, uint8_t{0xF}}) {
            SCOPED_TRACE(static_cast<int>(opcode));
            ExpectViolationOf(EncodeFrame(opcode, {}, kMask), WebSocketCloseCode::kProtocolError);
        }
    }

    TEST(WebSocketFrameTest, ControlFramesAreShortAndWhole) {
        const std::string payload126(126, 'p');
        ExpectViolationOf(EncodeFrame(WebSocketOpcode::kPing, TextBytes(payload126), kMask), WebSocketCloseCode::kProtocolError);
        ExpectViolationOf(EncodeFrame(WebSocketOpcode::kPing, TextBytes("x"), kMask, /*fin=*/false), WebSocketCloseCode::kProtocolError);
        ExpectViolationOf(EncodeFrame(WebSocketOpcode::kClose, {}, kMask, /*fin=*/false), WebSocketCloseCode::kProtocolError);

        const std::string payload125(125, 'p');
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes(payload125), kMask)));
        EXPECT_EQ(PopMessage(parser).payload.size(), 125u);
    }

    TEST(WebSocketFrameTest, ContinuationWithoutAStartIsAProtocolError) {
        ExpectViolationOf(EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("lo"), kMask), WebSocketCloseCode::kProtocolError);
    }

    TEST(WebSocketFrameTest, NewMessageBeforeTheEndOfTheFragmentsIsAProtocolError) {
        Vector<uint8_t> bytes = EncodeFrame(WebSocketOpcode::kText, TextBytes("Hel"), kMask, /*fin=*/false);
        const Vector<uint8_t> intruder = EncodeTextFrame("other", kMask);
        bytes.insert(bytes.end(), intruder.begin(), intruder.end());
        ExpectViolationOf(bytes, WebSocketCloseCode::kProtocolError);
    }

    TEST(WebSocketFrameTest, MessageOverTheLimitIsTooBig) {
        const std::string payload(11, 'x');
        ExpectViolationOf(EncodeTextFrame(FromStd(payload), kMask), WebSocketCloseCode::kMessageTooBig, /*maxMessageBytes=*/10);

        // The limit itself is fine
        WebSocketFrameParser parser(10);
        parser.Feed(AsSpan(EncodeTextFrame("0123456789", kMask)));
        EXPECT_EQ(TextOf(PopMessage(parser)), "0123456789");
    }

    TEST(WebSocketFrameTest, LimitIsCheckedOnTheAnnouncedLengthBeforeThePayloadArrives) {
        // The header of a frame that announces a megabyte more than the limit, and nothing after it
        const uint64_t length = ControlService::kMaxRequestBytes + 1;
        Vector<uint8_t> header = Bytes({0x81, 0xFF});
        for (int shift = 56; shift >= 0; shift -= 8) {
            header.push_back(static_cast<uint8_t>(length >> shift));
        }
        ExpectViolationOf(header, WebSocketCloseCode::kMessageTooBig);
    }

    TEST(WebSocketFrameTest, LengthWithTheTopBitSetIsTooBig) {
        const Vector<uint8_t> header = Bytes({0x81, 0xFF, 0x80, 0, 0, 0, 0, 0, 0, 0});
        ExpectViolationOf(header, WebSocketCloseCode::kMessageTooBig);
    }

    TEST(WebSocketFrameTest, FragmentsTogetherCountAgainstTheLimit) {
        Vector<uint8_t> bytes = EncodeFrame(WebSocketOpcode::kText, TextBytes("123456"), kMask, /*fin=*/false);
        const Vector<uint8_t> more = EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("78901"), kMask, /*fin=*/true);
        bytes.insert(bytes.end(), more.begin(), more.end());
        ExpectViolationOf(bytes, WebSocketCloseCode::kMessageTooBig, /*maxMessageBytes=*/10);
    }

    TEST(WebSocketFrameTest, ControlFramesDoNotCountAgainstTheLimit) {
        WebSocketFrameParser parser(10);
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes("0123456789012345"), kMask)));
        EXPECT_EQ(PopMessage(parser).kind, WebSocketMessageKind::Ping);
    }

    TEST(WebSocketFrameTest, FirstViolationIsFinal) {
        WebSocketFrameParser parser;
        parser.Feed(AsSpan(EncodeTextFrame("unmasked")));
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);

        parser.Feed(AsSpan(EncodeTextFrame("fine", kMask)));
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
    }

    TEST(WebSocketFrameTest, IncompleteHeadersWaitForMoreBytes) {
        const Vector<uint8_t> frame = EncodeFrame(WebSocketOpcode::kText, TextBytes(std::string(300, 'a')), kMask);
        // 1 byte, the first byte of the 16-bit length, the whole length but not the mask, the mask but not all of the payload
        for (const size_t size : {size_t{1}, size_t{3}, size_t{4}, size_t{7}, size_t{100}}) {
            WebSocketFrameParser parser;
            parser.Feed(Span<const uint8_t>(frame.data(), size));
            SCOPED_TRACE(size);
            ExpectNothingReady(parser);
        }
    }

    // The role of a client that reads a server: its frames are not masked

    TEST(WebSocketFrameTest, ServerRoleReadsUnmaskedTextBinaryPingAndClose) {
        WebSocketFrameParser parser(1024, WebSocketPeer::Server);
        parser.Feed(AsSpan(EncodeTextFrame("Hello")));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kBinary, TextBytes("bin"))));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes("p"))));
        parser.Feed(AsSpan(EncodeCloseFrame(1001, "bye")));

        const WebSocketMessage text = PopMessage(parser);
        EXPECT_EQ(text.kind, WebSocketMessageKind::Text);
        EXPECT_EQ(TextOf(text), "Hello");
        const WebSocketMessage binary = PopMessage(parser);
        EXPECT_EQ(binary.kind, WebSocketMessageKind::Binary);
        EXPECT_EQ(TextOf(binary), "bin");
        const WebSocketMessage ping = PopMessage(parser);
        EXPECT_EQ(ping.kind, WebSocketMessageKind::Ping);
        EXPECT_EQ(TextOf(ping), "p");
        const WebSocketMessage close = PopMessage(parser);
        EXPECT_EQ(close.kind, WebSocketMessageKind::Close);
        EXPECT_EQ(close.closeCode, 1001);
        EXPECT_EQ(TextOf(close), "bye");
        ExpectNothingReady(parser);
    }

    TEST(WebSocketFrameTest, ServerRoleReadsFragmentsAndEveryLengthForm) {
        WebSocketFrameParser parser(200000, WebSocketPeer::Server);
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kText, TextBytes("He"), std::nullopt, /*fin=*/false)));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes("x"))));
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("llo"), std::nullopt, /*fin=*/true)));
        EXPECT_EQ(PopMessage(parser).kind, WebSocketMessageKind::Ping);
        EXPECT_EQ(TextOf(PopMessage(parser)), "Hello");

        for (const size_t length : {0u, 125u, 126u, 65535u, 65536u, 100000u}) {
            const std::string payload(length, 'y');
            parser.Feed(AsSpan(EncodeTextFrame(FromStd(payload))));
            EXPECT_EQ(TextOf(PopMessage(parser)), payload) << length;
        }
    }

    TEST(WebSocketFrameTest, ServerRoleWaitsForTheRestOfAnUnmaskedFrame) {
        const Vector<uint8_t> frame = EncodeFrame(WebSocketOpcode::kText, TextBytes(std::string(300, 'a')));
        WebSocketFrameParser parser(1024, WebSocketPeer::Server);
        // 1 byte, the first byte of the 16-bit length, the whole header, some of the payload
        for (const size_t size : {size_t{1}, size_t{3}, size_t{4}, size_t{100}}) {
            WebSocketFrameParser partial(1024, WebSocketPeer::Server);
            partial.Feed(Span<const uint8_t>(frame.data(), size));
            SCOPED_TRACE(size);
            ExpectNothingReady(partial);
        }
        parser.Feed(AsSpan(frame));
        EXPECT_EQ(TextOf(PopMessage(parser)).size(), 300u);
    }

    TEST(WebSocketFrameTest, ServerRoleRefusesAMaskedFrame) {
        WebSocketFrameParser parser(1024, WebSocketPeer::Server);
        parser.Feed(AsSpan(EncodeTextFrame("masked", kMask)));
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
    }

    TEST(WebSocketFrameTest, ServerRoleKeepsTheSharedRules) {
        {
            WebSocketFrameParser parser(10, WebSocketPeer::Server);
            parser.Feed(AsSpan(EncodeTextFrame("0123456789A")));
            ExpectViolation(parser, WebSocketCloseCode::kMessageTooBig);
        }
        {
            WebSocketFrameParser parser(1024, WebSocketPeer::Server);
            parser.Feed(AsSpan(Bytes({0xC1, 0x00})));  // RSV1
            ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
        }
        {
            WebSocketFrameParser parser(1024, WebSocketPeer::Server);
            parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kPing, TextBytes("p"), std::nullopt, /*fin=*/false)));
            ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
        }
    }

    TEST(WebSocketFrameTest, TheDefaultRoleStillRefusesAnUnmaskedFrame) {
        WebSocketFrameParser parser(1024);
        parser.Feed(AsSpan(EncodeTextFrame("unmasked")));
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
    }

    // What the parser keeps: nothing beyond one incomplete frame, and nothing at all after an error

    TEST(WebSocketFrameTest, FramesThatArePoppedLeaveNothingBehind) {
        constexpr size_t kFrames = 20000;
        constexpr size_t kPieceFrames = 7;  // frames per Feed(), so a frame is cut by a piece now and then
        Vector<uint8_t> piece;
        for (size_t i = 0; i < kPieceFrames; ++i) {
            const auto frame = EncodeFrame(WebSocketOpcode::kPing, {}, kMask);
            piece.insert(piece.end(), frame.begin(), frame.end());
        }
        WebSocketFrameParser parser;
        size_t popped = 0;
        for (size_t fed = 0; fed < kFrames; fed += kPieceFrames) {
            parser.Feed(AsSpan(piece));
            while (true) {
                PopResult result = parser.Pop();
                ASSERT_TRUE(result.has_value());
                if (!result->has_value()) {
                    break;
                }
                ++popped;
            }
            ASSERT_EQ(parser.BufferedBytes(), 0u);
        }
        EXPECT_GE(popped, kFrames);
    }

    TEST(WebSocketFrameTest, ManyEmptyFragmentsHoldNothingAndTheLimitStopsTheRest) {
        constexpr size_t kLimit = 100;
        WebSocketFrameParser parser(kLimit);
        parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kText, {}, kMask, false)));
        for (size_t i = 0; i < 5000; ++i) {
            parser.Feed(AsSpan(EncodeFrame(WebSocketOpcode::kContinuation, {}, kMask, false)));
        }
        ExpectNothingReady(parser);
        EXPECT_EQ(parser.BufferedBytes(), 0u);

        // One byte per fragment fills the message up to the limit and the next one is too much
        const Vector<uint8_t> oneByte = EncodeFrame(WebSocketOpcode::kContinuation, TextBytes("x"), kMask, false);
        for (size_t i = 0; i < kLimit; ++i) {
            parser.Feed(AsSpan(oneByte));
            ExpectNothingReady(parser);
        }
        EXPECT_EQ(parser.BufferedBytes(), kLimit);
        parser.Feed(AsSpan(oneByte));
        ExpectViolation(parser, WebSocketCloseCode::kMessageTooBig);
        EXPECT_EQ(parser.BufferedBytes(), 0u) << "the fragments are dropped with the connection";
    }

    TEST(WebSocketFrameTest, AfterAnErrorNothingIsKeptAndNothingIsAccepted) {
        WebSocketFrameParser parser(1024);
        // A violation, then more than the parser would ever hold
        Vector<uint8_t> stream = Bytes({0xF1, 0x80, 0, 0, 0, 0});  // reserved bits
        stream.resize(4096, 0x41);
        parser.Feed(AsSpan(stream));
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);
        EXPECT_EQ(parser.BufferedBytes(), 0u);

        parser.Feed(AsSpan(stream));
        parser.Feed(AsSpan(EncodeTextFrame("fine", kMask)));
        EXPECT_EQ(parser.BufferedBytes(), 0u);
        ExpectViolation(parser, WebSocketCloseCode::kProtocolError);  // still the first error, and no message
    }

    TEST(WebSocketFrameTest, AnIncompleteFrameIsHeldUpToTheLimitAndNotMore) {
        constexpr size_t kLimit = 1000;
        WebSocketFrameParser parser(kLimit);
        const Vector<uint8_t> frame = EncodeFrame(WebSocketOpcode::kBinary, TextBytes(std::string(kLimit, 'p')), kMask);
        for (size_t i = 0; i + 1 < frame.size(); ++i) {
            parser.Feed(AsSpan(Bytes({frame[i]})));
            ExpectNothingReady(parser);
            ASSERT_LE(parser.BufferedBytes(), frame.size());
        }
        parser.Feed(AsSpan(Bytes({frame.back()})));
        EXPECT_EQ(PopMessage(parser).payload.size(), kLimit);
    }

}  // namespace BECore::Tests
