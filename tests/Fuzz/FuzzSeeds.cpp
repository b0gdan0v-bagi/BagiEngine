#include "FuzzSeeds.h"

#include "FuzzProbeAccess.h"
#include "FuzzTargets.h"
#include "FuzzWebSocketInput.h"

#include <BECore/Reflection/BinaryFormat.h>
#include <BECore/Reflection/BinarySerializer.h>
#include <BECore/Reflection/XmlSerializer.h>
#include <RemoteControl/WebSocketClientHandshake.h>

namespace BECore::Fuzz {

    namespace {

        constexpr const char* kBinaryFolder = "binary";
        constexpr const char* kXmlFolder = "xml";
        constexpr const char* kSaveFolder = "save";
        constexpr const char* kSceneFolder = "scene";
        constexpr const char* kWebSocketFrameFolder = "ws_frame";
        constexpr const char* kWebSocketHandshakeFolder = "ws_handshake";

        /// The game id and the schema versions of the save seeds against the schema of FuzzSaveEnvelope (game "FuzzGame", writes 3, reads 2 and 3)
        constexpr StringView kSaveGame = "FuzzGame";
        constexpr uint32_t kSaveVersionCurrent = 3;
        constexpr uint32_t kSaveVersionOldest = 2;
        constexpr uint32_t kSaveVersionTooOld = 1;
        constexpr uint32_t kSaveVersionTooNew = 4;

        /// A scene that references the node file with overrides of the node and of one component
        constexpr StringView kSceneWithOverrides = R"(<SceneManager><scenes><scene type="Scene" name="Main" viewWidth="800" viewHeight="600"><nodes>
  <node name="Plain"/>
  <node ref="nodes/Fuzz" name="Renamed" layer="3"><components><component type="TransformComponent" x="40"/></components></node>
</nodes></scene></scenes></SceneManager>)";

        /// A scene with a reference and no override, and one that the node file cannot satisfy
        constexpr StringView kSceneWithReference =
            R"(<SceneManager><scenes><scene type="Scene" name="Main"><nodes><node ref="nodes/Fuzz"/><node ref="nodes/Missing"/></nodes></scene></scenes></SceneManager>)";

        /// A node file of the built-in components
        constexpr StringView kBoxNodeFile = R"(<node name="Box" layer="2"><components>
  <component type="TransformComponent" x="1" y="2" width="30" height="40"/>
  <component type="QuadRendererComponent"><color r="1" g="2" b="3" a="255"/></component>
  <component type="ClickableComponent" actionName="box"/>
</components></node>)";

        /// A node file with a canvas of UI elements
        constexpr StringView kCanvasNodeFile = R"(<node name="Panel"><components>
  <component type="UICanvasComponent" space="Screen" scaleMode="ConstantPixelSize">
    <elements>
      <element type="UIElement" name="Title" x="8" y="8" width="184" height="30" fontSize="24">
        <components>
          <component type="UITextComponent" text="Base"/>
        </components>
      </element>
    </elements>
  </component>
</components></node>)";

        /// How many bytes a cut from the end takes off the stream that "truncated" is made from
        constexpr size_t kTruncatedBytes = 9;

        /// Offset of the u32 length of the only string entry of MakeStringStream(): header, tag, name length, the name "s"
        constexpr size_t kStringLengthOffset = BinaryFormat::HeaderSize + 1 + sizeof(uint16_t) + 1;

        /// Offset of the u32 count of the only array entry of MakeArrayStream(): header, tag, name length, the name "a", the size
        constexpr size_t kArrayCountOffset = BinaryFormat::HeaderSize + 1 + sizeof(uint16_t) + 1 + sizeof(uint32_t);

        constexpr uint32_t kHugeLength = 0xFFFFFFFFu;

        FuzzBytes ToBytes(const BinarySerializer& serializer) {
            const eastl::vector<uint8_t>& data = serializer.GetData();
            return FuzzBytes(data.begin(), data.end());
        }

        /// A stream with every tag, nested objects, an array of objects, an array of values and a string
        FuzzBytes MakeAllTagsStream() {
            BinarySerializer writer;
            writer.WriteAttribute("flag", true);
            writer.WriteAttribute("i8", static_cast<int8_t>(-8));
            writer.WriteAttribute("u8", static_cast<uint8_t>(200));
            writer.WriteAttribute("i16", static_cast<int16_t>(-1600));
            writer.WriteAttribute("u16", static_cast<uint16_t>(60000));
            writer.WriteAttribute("i32", static_cast<int32_t>(-320000));
            writer.WriteAttribute("u32", static_cast<uint32_t>(4000000000u));
            writer.WriteAttribute("i64", static_cast<int64_t>(-6400000000000));
            writer.WriteAttribute("u64", static_cast<uint64_t>(18000000000000000000ull));
            writer.WriteAttribute("f32", 1.5f);
            writer.WriteAttribute("f64", -2.25);
            writer.WriteAttribute("text", StringView("hello"));

            if (writer.BeginObject("player")) {
                writer.WriteAttribute("health", static_cast<int32_t>(100));
                writer.WriteAttribute("name", StringView("Hero"));
                if (writer.BeginObject("position")) {
                    writer.WriteAttribute("x", 1.0f);
                    writer.WriteAttribute("y", 2.0f);
                    writer.EndObject();
                }
                writer.EndObject();
            }

            size_t itemCount = 3;
            if (writer.BeginArray("items", "item", itemCount)) {
                for (int32_t id = 0; id < 3; ++id) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("id", id);
                        writer.WriteAttribute("label", StringView(id == 0 ? "first" : "other"));
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }

            size_t valueCount = 4;
            if (writer.BeginArray("values", "v", valueCount)) {
                for (int32_t value = 10; value < 14; ++value) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("value", value);
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }
            return ToBytes(writer);
        }

        /// Only the string "s" = "abc"
        FuzzBytes MakeStringStream() {
            BinarySerializer writer;
            writer.WriteAttribute("s", StringView("abc"));
            return ToBytes(writer);
        }

        /// Only an array "a" of two elements with one value each
        FuzzBytes MakeArrayStream() {
            BinarySerializer writer;
            size_t count = 2;
            if (writer.BeginArray("a", "e", count)) {
                for (int32_t value = 0; value < 2; ++value) {
                    if (writer.BeginArrayElement()) {
                        writer.WriteAttribute("v", value);
                        writer.EndArrayElement();
                    }
                }
                writer.EndArray();
            }
            return ToBytes(writer);
        }

        FuzzBytes TextBytes(StringView text) {
            const auto* begin = reinterpret_cast<const uint8_t*>(text.data());
            return FuzzBytes(begin, begin + text.size());
        }

        /// The input of FuzzSceneNode: the scene text, a NUL byte, the node file
        FuzzBytes SceneAndNode(StringView scene, StringView node) {
            FuzzBytes bytes = TextBytes(scene);
            bytes.push_back(0);
            const FuzzBytes nodeBytes = TextBytes(node);
            bytes.insert(bytes.end(), nodeBytes.begin(), nodeBytes.end());
            return bytes;
        }

        /// The probe state as the XML a content file holds
        FuzzBytes MakeProbeXml() {
            XmlSerializer writer;
            MakeFilledProbe().Serialize(writer);
            const eastl::string text = writer.SaveToString();
            return TextBytes(StringView(text));
        }

        void StoreWord(FuzzBytes& bytes, size_t offset, uint32_t value) {
            std::memcpy(bytes.data() + offset, &value, sizeof(value));
        }

        /// Make the payload size of the header true again after the stream was cut
        void FixPayloadSize(FuzzBytes& bytes) {
            StoreWord(bytes, BinaryFormat::PayloadSizeOffset, static_cast<uint32_t>(bytes.size() - BinaryFormat::HeaderSize));
        }

        // =========================================================================
        // WebSocket frames and handshakes
        // =========================================================================

        using WebSocketInput::HandshakePolicyKind;
        using WebSocketInput::PackFrameInput;

        constexpr WebSocketMask kRfcMask = {0x37, 0xFA, 0x21, 0x3D};  // the key of the examples in RFC 6455, section 5.7

        /// Payload sizes at the edges of the three length forms of a frame
        constexpr size_t kLongestShortLength = 125;
        constexpr size_t kShortestMediumLength = 126;
        constexpr size_t kMediumPayloadBytes = 300;
        constexpr size_t kLimitIndexOf1024 = 4;
        constexpr size_t kLimitIndexOf125 = 2;
        constexpr size_t kLimitIndexOf65536 = 5;
        constexpr size_t kLimitIndexOf1 = 1;
        constexpr size_t kLongOriginLabelBytes = 200;
        constexpr uint8_t kChunkPattern = 3;      // a pattern that feeds a few bytes at a time
        constexpr uint8_t kLongChunkPattern = 7;  // chunks of up to 128 bytes

        /// The 8-byte length form of a frame that would fit the 2-byte one: the form is legal for any length (RFC 6455 section 5.2 asks for the shortest, parsers accept either)
        FuzzBytes LongLengthFrame(uint8_t opcode, StringView payload, const WebSocketMask& mask) {
            constexpr uint8_t kFin = 0x80;
            constexpr uint8_t kMaskBit = 0x80;
            constexpr uint8_t kLength64Marker = 127;
            FuzzBytes frame;
            frame.push_back(kFin | opcode);
            frame.push_back(kMaskBit | kLength64Marker);
            for (int shift = 56; shift >= 0; shift -= 8) {
                frame.push_back(static_cast<uint8_t>(static_cast<uint64_t>(payload.size()) >> shift));
            }
            frame.insert(frame.end(), mask.data(), mask.data() + mask.size());
            for (size_t i = 0; i < payload.size(); ++i) {
                frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % mask.size()]);
            }
            return frame;
        }

        eastl::span<const uint8_t> PayloadOf(StringView text) {
            return eastl::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        }

        /// A frame input: the selector byte, then the frames in order
        FuzzBytes FrameInput(WebSocketPeer peer, size_t limitIndex, uint8_t pattern, std::initializer_list<FuzzBytes> frames) {
            FuzzBytes bytes;
            bytes.push_back(PackFrameInput(peer, limitIndex, pattern));
            for (const FuzzBytes& frame : frames) {
                bytes.insert(bytes.end(), frame.begin(), frame.end());
            }
            return bytes;
        }

        FuzzBytes Frame(uint8_t opcode, StringView payload, Optional<WebSocketMask> mask, bool fin = true) {
            const eastl::vector<uint8_t> frame = EncodeFrame(opcode, PayloadOf(payload), mask, fin);
            return FuzzBytes(frame.begin(), frame.end());
        }

        FuzzBytes CloseFrame(uint16_t code, StringView reason, Optional<WebSocketMask> mask) {
            const eastl::vector<uint8_t> frame = EncodeCloseFrame(code, reason, mask);
            return FuzzBytes(frame.begin(), frame.end());
        }

        /// A handshake input: the policy byte, then the text
        FuzzBytes HandshakeInput(HandshakePolicyKind policy, StringView text) {
            FuzzBytes bytes;
            bytes.push_back(static_cast<uint8_t>(policy));
            const FuzzBytes textBytes = TextBytes(text);
            bytes.insert(bytes.end(), textBytes.begin(), textBytes.end());
            return bytes;
        }

        /// The request head of a browser (the RFC's example, and the header set of a current browser)
        constexpr StringView kRfcRequest = "GET /chat HTTP/1.1\r\nHost: server.example.com\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: "
                                           "dGhlIHNhbXBsZSBub25jZQ==\r\nOrigin: http://example.com\r\nSec-WebSocket-Version: 13\r\n\r\n";
        constexpr StringView kBrowserRequest =
            "GET / HTTP/1.1\r\nHost: rooms.example:8080\r\nConnection: Upgrade\r\nPragma: no-cache\r\nCache-Control: no-cache\r\nUser-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
            "AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\r\nUpgrade: websocket\r\nOrigin: https://good.example\r\nSec-WebSocket-Version: 13\r\nAccept-Encoding: gzip, deflate, br\r\n"
            "Accept-Language: en-US,en;q=0.9\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Extensions: permessage-deflate; client_max_window_bits\r\n\r\n";
        constexpr StringView kHealthRequest = "GET /healthz HTTP/1.1\r\nHost: rooms.example\r\nUser-Agent: kube-probe/1.28\r\nAccept: */*\r\nConnection: close\r\n\r\n";
        constexpr StringView kDuplicatedHeadersRequest =
            "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\nUpgrade: h2c\r\nUpgrade: websocket\r\nConnection: keep-alive\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
        constexpr StringView kUrlOfARoom = "wss://Rooms.Example.com/room?code=ABCD&name=x";
        constexpr StringView kUrlOfTheControlApi = "ws://127.0.0.1:8765/control";
        constexpr StringView kUrlWithABadPort = "ws://host:99999/path";

        /// A request head made by the client's own builder
        eastl::string ProgramRequest(StringView url, StringView origin) {
            const auto parsed = ParseWebSocketUrl(url);
            return BuildClientHandshakeRequest(*parsed, WebSocketInput::kHandshakeKey, origin);
        }

        /// A 101 response that also names an extension the request did not ask for: a head with one more header line before the empty line
        eastl::string WithExtensions(eastl::string response) {
            response.insert(response.size() - 2, "Sec-WebSocket-Extensions: permessage-deflate\r\n");
            return response;
        }

        void AddWebSocketSeeds(eastl::vector<FuzzSeedFile>& seeds) {
            using enum WebSocketPeer;
            const Optional<WebSocketMask> noMask;
            const Optional<WebSocketMask> mask = kRfcMask;
            const eastl::string mediumText(kMediumPayloadBytes, 'm');
            const eastl::string longestShortText(kLongestShortLength, 's');
            const eastl::string shortestMediumText(kShortestMediumLength, 'p');

            seeds.push_back({kWebSocketFrameFolder, "client-hello.bin", FrameInput(Client, kLimitIndexOf65536, 0, {Frame(WebSocketOpcode::kText, "Hello", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-hello-chunked.bin", FrameInput(Client, kLimitIndexOf65536, kChunkPattern, {Frame(WebSocketOpcode::kText, "Hello", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-length-forms.bin",
                             FrameInput(Client, kLimitIndexOf65536, kLongChunkPattern,
                                        {Frame(WebSocketOpcode::kText, "", mask), Frame(WebSocketOpcode::kText, longestShortText, mask), Frame(WebSocketOpcode::kBinary, shortestMediumText, mask),
                                         Frame(WebSocketOpcode::kText, mediumText, mask), LongLengthFrame(WebSocketOpcode::kText, "long form", kRfcMask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-fragments-with-ping.bin",
                             FrameInput(Client, kLimitIndexOf1024, kChunkPattern,
                                        {Frame(WebSocketOpcode::kText, "Hel", mask, false), Frame(WebSocketOpcode::kPing, "are you there", mask),
                                         Frame(WebSocketOpcode::kContinuation, "lo ", mask, false), Frame(WebSocketOpcode::kPong, "", mask), Frame(WebSocketOpcode::kContinuation, "world", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-close.bin",
                             FrameInput(Client, kLimitIndexOf1024, 0, {Frame(WebSocketOpcode::kText, "bye", mask), CloseFrame(WebSocketCloseCode::kNormal, "done", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-close-without-code.bin", FrameInput(Client, kLimitIndexOf1024, 0, {Frame(WebSocketOpcode::kClose, "", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-close-reserved-code.bin", FrameInput(Client, kLimitIndexOf1024, 0, {CloseFrame(WebSocketCloseCode::kNoStatusReceived, "", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-unmasked.bin", FrameInput(Client, kLimitIndexOf1024, 0, {Frame(WebSocketOpcode::kText, "Hello", noMask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-over-the-limit.bin", FrameInput(Client, kLimitIndexOf125, 0, {Frame(WebSocketOpcode::kText, shortestMediumText, mask)})});
            seeds.push_back({kWebSocketFrameFolder, "client-two-tiny-limit.bin",
                             FrameInput(Client, kLimitIndexOf1, kChunkPattern, {Frame(WebSocketOpcode::kText, "a", mask), Frame(WebSocketOpcode::kText, "bc", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "server-hello.bin", FrameInput(Server, kLimitIndexOf65536, 0, {Frame(WebSocketOpcode::kText, "Hello", noMask)})});
            seeds.push_back({kWebSocketFrameFolder, "server-fragments.bin",
                             FrameInput(Server, kLimitIndexOf1024, kChunkPattern,
                                        {Frame(WebSocketOpcode::kBinary, "ab", noMask, false), Frame(WebSocketOpcode::kPing, "p", noMask), Frame(WebSocketOpcode::kContinuation, "cd", noMask)})});
            seeds.push_back({kWebSocketFrameFolder, "server-masked.bin", FrameInput(Server, kLimitIndexOf1024, 0, {Frame(WebSocketOpcode::kText, "Hello", mask)})});
            seeds.push_back({kWebSocketFrameFolder, "server-close.bin", FrameInput(Server, kLimitIndexOf1024, 0, {CloseFrame(WebSocketCloseCode::kGoingAway, "restart", noMask)})});

            seeds.push_back({kWebSocketHandshakeFolder, "request-rfc-example.bin", HandshakeInput(HandshakePolicyKind::PublicAnyOrigin, kRfcRequest)});
            seeds.push_back({kWebSocketHandshakeFolder, "request-browser.bin", HandshakeInput(HandshakePolicyKind::PublicWithOrigins, kBrowserRequest)});
            seeds.push_back({kWebSocketHandshakeFolder, "request-program-loopback.bin", HandshakeInput(HandshakePolicyKind::Loopback, ProgramRequest(kUrlOfTheControlApi, {}))});
            seeds.push_back({kWebSocketHandshakeFolder, "request-program-public.bin", HandshakeInput(HandshakePolicyKind::PublicNoMissingOrigin, ProgramRequest(kUrlOfARoom, "https://good.example"))});
            seeds.push_back({kWebSocketHandshakeFolder, "request-health-check.bin", HandshakeInput(HandshakePolicyKind::PublicWithHealthPath, kHealthRequest)});
            seeds.push_back({kWebSocketHandshakeFolder, "request-duplicated-headers.bin", HandshakeInput(HandshakePolicyKind::PublicAnyOrigin, kDuplicatedHeadersRequest)});
            seeds.push_back({kWebSocketHandshakeFolder, "request-long-origin.bin",
                             HandshakeInput(HandshakePolicyKind::PublicWithOrigins, ProgramRequest(kUrlOfARoom, Format("https://{}.example", eastl::string(kLongOriginLabelBytes, 'a'))))});
            seeds.push_back({kWebSocketHandshakeFolder, "response-accept.bin", HandshakeInput(HandshakePolicyKind::Loopback, BuildAcceptResponse(WebSocketInput::kHandshakeKey))});
            seeds.push_back({kWebSocketHandshakeFolder, "response-refusal.bin",
                             HandshakeInput(HandshakePolicyKind::Loopback, BuildRejectResponse(HandshakeError{403, "the Origin 'https://evil.example' is not allowed by this server"}))});
            seeds.push_back(
                {kWebSocketHandshakeFolder, "response-with-extensions.bin", HandshakeInput(HandshakePolicyKind::Loopback, WithExtensions(BuildAcceptResponse(WebSocketInput::kHandshakeKey)))});
            seeds.push_back({kWebSocketHandshakeFolder, "url-room.bin", HandshakeInput(HandshakePolicyKind::Loopback, kUrlOfARoom)});
            seeds.push_back({kWebSocketHandshakeFolder, "url-control-api.bin", HandshakeInput(HandshakePolicyKind::Loopback, kUrlOfTheControlApi)});
            seeds.push_back({kWebSocketHandshakeFolder, "url-bad-port.bin", HandshakeInput(HandshakePolicyKind::Loopback, kUrlWithABadPort)});
        }

    }  // namespace

    eastl::vector<FuzzSeedFile> MakeGeneratedSeeds() {
        eastl::vector<FuzzSeedFile> seeds;

        const FuzzBytes allTags = MakeAllTagsStream();
        seeds.push_back({kBinaryFolder, "all-tags.bin", allTags});

        // The header is right and the last entry is cut: the sizes inside lie
        FuzzBytes truncated = allTags;
        truncated.resize(truncated.size() - kTruncatedBytes);
        FixPayloadSize(truncated);
        seeds.push_back({kBinaryFolder, "truncated.bin", truncated});

        // Cut with no care for the header
        FuzzBytes shortStream = allTags;
        shortStream.resize(BinaryFormat::HeaderSize + 5);
        seeds.push_back({kBinaryFolder, "header-only-cut.bin", shortStream});

        FuzzBytes hugeString = MakeStringStream();
        StoreWord(hugeString, kStringLengthOffset, kHugeLength);
        seeds.push_back({kBinaryFolder, "string-length-ffffffff.bin", hugeString});

        FuzzBytes hugeCount = MakeArrayStream();
        StoreWord(hugeCount, kArrayCountOffset, kHugeLength);
        seeds.push_back({kBinaryFolder, "array-count-ffffffff.bin", hugeCount});

        seeds.push_back({kBinaryFolder, "array.bin", MakeArrayStream()});

        seeds.push_back({kXmlFolder, "probe-filled.xml", MakeProbeXml()});

        // Saves of the schema FuzzSaveEnvelope loads with: the current version, the oldest it still reads, one too new, one too old, another game
        seeds.push_back({kSaveFolder, "save-current.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionCurrent)});
        seeds.push_back({kSaveFolder, "save-oldest.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionOldest)});
        seeds.push_back({kSaveFolder, "save-too-old.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionTooOld)});
        seeds.push_back({kSaveFolder, "save-too-new.bin", MakeSaveFileBytes(kSaveGame, kSaveVersionTooNew)});
        seeds.push_back({kSaveFolder, "save-other-game.bin", MakeSaveFileBytes("OtherGame", kSaveVersionCurrent)});

        seeds.push_back({kSceneFolder, "scene-overrides.bin", SceneAndNode(kSceneWithOverrides, kBoxNodeFile)});
        seeds.push_back({kSceneFolder, "scene-reference.bin", SceneAndNode(kSceneWithReference, kCanvasNodeFile)});
        seeds.push_back({kSceneFolder, "node-file-only.xml", TextBytes(kBoxNodeFile)});
        seeds.push_back({kSceneFolder, "node-file-canvas.xml", TextBytes(kCanvasNodeFile)});

        AddWebSocketSeeds(seeds);
        AddRoomAndControlSeeds(seeds);
        AddUiSeeds(seeds);
        return seeds;
    }

    bool WriteGeneratedSeeds(const std::filesystem::path& corpusRoot) {
        for (const FuzzSeedFile& seed : MakeGeneratedSeeds()) {
            const std::filesystem::path folder = corpusRoot / seed.folder;
            std::error_code error;
            std::filesystem::create_directories(folder, error);
            if (error) {
                return false;
            }
            std::ofstream file(folder / seed.name, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(seed.bytes.data()), static_cast<std::streamsize>(seed.bytes.size()));
            if (!file) {
                return false;
            }
        }
        return true;
    }

}  // namespace BECore::Fuzz
