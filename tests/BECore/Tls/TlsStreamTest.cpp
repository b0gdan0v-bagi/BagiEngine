#include "FakeTlsStream.h"

#include <RemoteControl/Tls/TlsStream.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr auto kWaitLimit = 20s;

        /// The server end of the fake backend: a listener whose first connection does the fake handshake on a thread of its own
        class FakeServer {
        public:
            FakeServer() {
                auto listener = Socket::Listen(0);
                EXPECT_TRUE(listener.has_value());
                if (listener) {
                    _listener = std::move(*listener);
                }
            }

            ~FakeServer() {
                Join();
            }

            [[nodiscard]] uint16_t GetPort() const {
                return _listener.GetLocalPort();
            }

            /// Accepts the client and answers its hello (@p rejectReason empty: accepts it); @p stall keeps the answer back
            void StartHandshake(std::string rejectReason = {}, std::chrono::milliseconds stall = 0ms) {
                _thread = std::thread([this, rejectReason = std::move(rejectReason), stall] {
                    auto accepted = _listener.Accept(kWaitLimit);
                    if (!accepted) {
                        return;
                    }
                    _peer = std::move(*accepted);
                    std::this_thread::sleep_for(stall);
                    _done = _side.Handshake(_peer, rejectReason);
                });
            }

            void Join() {
                if (_thread.joinable()) {
                    _thread.join();
                }
            }

            /// Accepts the client and says nothing (a server that never answers the hello)
            void StartSilent() {
                _thread = std::thread([this] {
                    auto accepted = _listener.Accept(kWaitLimit);
                    if (accepted) {
                        _peer = std::move(*accepted);
                    }
                });
            }

            /// The bytes the client sent after the handshake, as they travel (masked) or after the server removed the mask
            std::string ReadFromClient(size_t count, bool unmask) {
                std::string bytes;
                _peer.SetReceiveTimeout(kWaitLimit);
                while (bytes.size() < count) {
                    std::array<uint8_t, 256> chunk;
                    const auto received = _peer.Receive(eastl::span<uint8_t>(chunk.data(), std::min(chunk.size(), count - bytes.size())));
                    if (!received) {
                        break;
                    }
                    if (unmask) {
                        FakeTlsMask(chunk.data(), *received);
                    }
                    bytes.append(reinterpret_cast<const char*>(chunk.data()), *received);
                }
                return bytes;
            }

            void SendToClient(const std::string& text) {
                std::string masked = text;
                FakeTlsMask(reinterpret_cast<uint8_t*>(masked.data()), masked.size());
                EXPECT_TRUE(_peer.SendAll(AsBytes(FromStd(masked))));
            }

            Socket& Peer() {
                return _peer;
            }

            [[nodiscard]] const FakeTlsServerSide& Side() const {
                return _side;
            }

            [[nodiscard]] bool Accepted() const {
                return _done;
            }

        private:
            Socket _listener;
            Socket _peer;
            FakeTlsServerSide _side;
            std::thread _thread;
            bool _done = false;  // written by the thread, read after Join()
        };

        [[nodiscard]] TlsConnectParams ParamsFor(const FakeServer& server, StringView host = "127.0.0.1") {
            TlsConnectParams params;
            params.host = host;
            params.port = server.GetPort();
            params.connectTimeout = 5s;
            params.handshakeDeadline = std::chrono::steady_clock::now() + kWaitLimit;
            return params;
        }

        /// A connected pair: the client stream and the server that accepted it
        void Connect(FakeServer& server, FakeTlsStream& stream) {
            server.StartHandshake();
            const auto connected = stream.Connect(ParamsFor(server));
            server.Join();
            ASSERT_TRUE(connected.has_value()) << connected.error().c_str();
            ASSERT_TRUE(server.Accepted());
            ASSERT_TRUE(stream.IsValid());
        }

        template <typename Predicate>
        bool WaitUntil(Predicate done) {
            const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
            while (std::chrono::steady_clock::now() < deadline) {
                if (done()) {
                    return true;
                }
                std::this_thread::sleep_for(1ms);
            }
            return false;
        }

    }  // namespace

    TEST(TlsStreamTest, TheStubRefusesWithTheUnsupportedText) {
        if (IsTlsSupported()) {
            GTEST_SKIP() << "this platform has a TLS backend";
        }
        auto stream = MakeTlsStream();
        ASSERT_NE(stream, nullptr);
        TlsConnectParams params;
        params.host = "example.com";
        params.port = 443;
        const auto connected = stream->Connect(params);
        ASSERT_FALSE(connected.has_value());
        EXPECT_TRUE(std::string(connected.error().c_str()).starts_with("tls_unsupported: this platform has no TLS backend")) << connected.error().c_str();
        EXPECT_EQ(stream->GetLastError(), connected.error());
        EXPECT_FALSE(stream->IsValid());
    }

    TEST(TlsStreamTest, AStreamThatIsNotConnectedFailsReceiveAndSend) {
        FakeTlsStream fake;
        auto stub = MakeTlsStream();
        for (ITlsStream* stream : {static_cast<ITlsStream*>(&fake), stub.get()}) {
            std::array<uint8_t, 8> buffer{};
            const auto received = stream->Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
            ASSERT_FALSE(received.has_value());
            EXPECT_EQ(received.error(), SocketError::Failed);
            EXPECT_FALSE(stream->SendAll(eastl::span<const uint8_t>(buffer.data(), buffer.size())));
            EXPECT_FALSE(stream->IsValid());
            stream->ShutdownBoth();  // harmless in every state
            stream->Close();
        }
        EXPECT_FALSE(fake.GetLastError().empty());
    }

    TEST(TlsStreamTest, TheFakePassesTheServerNameAndTheAnchors) {
        FakeServer server;
        FakeTlsStream stream;
        const TlsTrustAnchors anchors = {{1, 2, 3}, {4, 5}};
        TlsConnectParams params = ParamsFor(server, "localhost");
        params.additionalTrustAnchorsDer = eastl::span<const eastl::vector<uint8_t>>(anchors.data(), anchors.size());
        server.StartHandshake();
        const auto connected = stream.Connect(params);
        server.Join();
        ASSERT_TRUE(connected.has_value()) << connected.error().c_str();
        EXPECT_EQ(server.Side().serverName, "localhost");
        EXPECT_EQ(server.Side().anchorCount, 2u);
        EXPECT_TRUE(stream.IsValid());
    }

    TEST(TlsStreamTest, ARejectedHandshakeFailsWithTheReasonOfTheServer) {
        FakeServer server;
        FakeTlsStream stream;
        server.StartHandshake("certificate is not trusted");
        const auto connected = stream.Connect(ParamsFor(server));
        server.Join();
        ASSERT_FALSE(connected.has_value());
        EXPECT_NE(std::string(connected.error().c_str()).find("certificate is not trusted"), std::string::npos) << connected.error().c_str();
        EXPECT_EQ(stream.GetLastError(), connected.error());
        EXPECT_FALSE(stream.IsValid());
        std::array<uint8_t, 8> buffer{};
        EXPECT_FALSE(stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size())).has_value());
    }

    TEST(TlsStreamTest, AConnectionRefusedNamesTheAddress) {
        uint16_t closedPort = 0;
        {
            FakeServer server;  // its port is free again when it is gone
            closedPort = server.GetPort();
        }
        FakeTlsStream stream;
        TlsConnectParams params;
        params.host = "127.0.0.1";
        params.port = closedPort;
        params.connectTimeout = 5s;
        const auto connected = stream.Connect(params);
        ASSERT_FALSE(connected.has_value());
        EXPECT_NE(std::string(connected.error().c_str()).find("127.0.0.1:" + std::to_string(closedPort)), std::string::npos) << connected.error().c_str();
    }

    TEST(TlsStreamTest, AServerThatNeverAnswersTheHelloEndsAtTheDeadline) {
        FakeServer server;
        FakeTlsStream stream;
        server.StartSilent();
        TlsConnectParams params = ParamsFor(server);
        params.handshakeDeadline = std::chrono::steady_clock::now() + 150ms;
        const auto started = std::chrono::steady_clock::now();
        const auto connected = stream.Connect(params);
        ASSERT_FALSE(connected.has_value());
        EXPECT_NE(std::string(connected.error().c_str()).find("timed out"), std::string::npos) << connected.error().c_str();
        EXPECT_LT(std::chrono::steady_clock::now() - started, 10s);
        server.Join();
    }

    TEST(TlsStreamTest, TheCancelFlagEndsAHandshakeThatWaits) {
        FakeServer server;
        FakeTlsStream stream;
        server.StartSilent();
        std::atomic<bool> cancel{false};
        TlsConnectParams params = ParamsFor(server);
        params.cancel = &cancel;
        std::atomic<bool> ended{false};
        std::expected<void, eastl::string> connected;
        std::thread connecting([&] {
            connected = stream.Connect(params);
            ended = true;
        });
        std::this_thread::sleep_for(100ms);
        EXPECT_FALSE(ended.load());
        cancel = true;
        EXPECT_TRUE(WaitUntil([&] { return ended.load(); }));
        connecting.join();
        ASSERT_FALSE(connected.has_value());
        EXPECT_NE(std::string(connected.error().c_str()).find("cancelled"), std::string::npos) << connected.error().c_str();
        server.Join();
    }

    TEST(TlsStreamTest, DataTravelsBothWaysMaskedAndTheEndOfTheStreamIsClosed) {
        FakeServer server;
        FakeTlsStream stream;
        ASSERT_NO_FATAL_FAILURE(Connect(server, stream));

        const std::string hello = "hello server";
        ASSERT_TRUE(stream.SendAll(BECore::AsBytes(FromStd(hello))));
        const std::string onTheWire = server.ReadFromClient(hello.size(), /*unmask=*/false);
        EXPECT_NE(onTheWire, hello) << "the fake has to change the bytes, or a missing 'encryption' goes unnoticed";
        std::string unmasked = onTheWire;
        FakeTlsMask(reinterpret_cast<uint8_t*>(unmasked.data()), unmasked.size());
        EXPECT_EQ(unmasked, hello);

        server.SendToClient("hello client");
        std::string received;
        while (received.size() < 12) {
            std::array<uint8_t, 64> buffer;
            const auto count = stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
            ASSERT_TRUE(count.has_value());
            ASSERT_GT(*count, 0u);
            received.append(reinterpret_cast<const char*>(buffer.data()), *count);
        }
        EXPECT_EQ(received, "hello client");

        server.Peer().ShutdownBoth();
        std::array<uint8_t, 8> buffer{};
        const auto ended = stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), SocketError::Closed);
    }

    TEST(TlsStreamTest, AReceiveTimeoutIsTimedOut) {
        FakeServer server;
        FakeTlsStream stream;
        ASSERT_NO_FATAL_FAILURE(Connect(server, stream));
        EXPECT_TRUE(stream.SetReceiveTimeout(50ms));
        std::array<uint8_t, 8> buffer{};
        const auto received = stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
        ASSERT_FALSE(received.has_value());
        EXPECT_EQ(received.error(), SocketError::TimedOut);
        EXPECT_TRUE(stream.IsValid()) << "a timeout does not end the stream";
    }

    // Winsock does not wake a blocked recv() on shutdown(), so a backend there waits in slices (ITlsStream's contract)
    TEST(TlsStreamTest, ShutdownBothWakesAReceiveThatWaitsInAnotherThread) {
        FakeServer server;
        FakeTlsStream stream;
        ASSERT_NO_FATAL_FAILURE(Connect(server, stream));

        std::atomic<bool> ended{false};
        std::atomic<bool> closed{false};
        std::thread reader([&] {
            std::array<uint8_t, 16> buffer;
            const auto received = stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
            closed = !received && received.error() == SocketError::Closed;
            ended = true;
        });
        std::this_thread::sleep_for(100ms);  // let the reader block; the server sends nothing and stays open
        EXPECT_FALSE(ended.load());
        stream.ShutdownBoth();
        EXPECT_TRUE(WaitUntil([&] { return ended.load(); }));
        reader.join();
        EXPECT_TRUE(closed.load());
    }

    TEST(TlsStreamTest, ReceiveAndSendAllWorkAtTheSameTimeFromTwoThreads) {
        FakeServer server;
        FakeTlsStream stream;
        ASSERT_NO_FATAL_FAILURE(Connect(server, stream));

        constexpr size_t kBytes = 256 * 1024;
        std::string payload(kBytes, 'x');
        for (size_t i = 0; i < payload.size(); ++i) {
            payload[i] = static_cast<char>('a' + i % 26);
        }

        // The server echoes what it reads, byte for byte, while the client sends and reads at once
        std::thread echo([&] {
            std::string seen = server.ReadFromClient(kBytes, /*unmask=*/true);
            server.SendToClient(seen);
        });
        std::thread sender([&] { EXPECT_TRUE(stream.SendAll(BECore::AsBytes(FromStd(payload)))); });

        std::string received;
        while (received.size() < kBytes) {
            std::array<uint8_t, 4096> buffer;
            const auto count = stream.Receive(eastl::span<uint8_t>(buffer.data(), buffer.size()));
            if (!count) {
                ADD_FAILURE() << "the stream ended after " << received.size() << " bytes";
                break;
            }
            received.append(reinterpret_cast<const char*>(buffer.data()), *count);
        }
        sender.join();
        echo.join();
        EXPECT_EQ(received, payload);
    }

}  // namespace BECore::Tests
