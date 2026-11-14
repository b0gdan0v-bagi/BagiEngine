#include "../WebSocketTestClient.h"

#include <RemoteControl/Tls/TlsStream.h>
#include <RemoteControl/WebSocketClient.h>
#include <gtest/gtest.h>
#include <iterator>
#include <thread>

// The real backend of this platform against real TLS servers that tls_local_check.py starts (it puts their ports and the test CA in
// the environment); run on its own, every case skips itself. The servers are Python's `ssl` with the fixtures of data/.

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr const char* kEnvPort = "BAGI_TLS_TEST_PORT";
        constexpr const char* kEnvExpiredPort = "BAGI_TLS_TEST_EXPIRED_PORT";
        constexpr const char* kEnvPlainPort = "BAGI_TLS_TEST_PLAIN_PORT";
        constexpr const char* kEnvCaFile = "BAGI_TLS_TEST_CA";

        constexpr size_t kLargeMessageBytes = 200 * 1024;  // several TLS records and several reads
        constexpr std::chrono::milliseconds kShortHandshake{1500};
        constexpr std::chrono::milliseconds kHandshakeSlack{3000};
        constexpr std::chrono::milliseconds kShortPause{300};

        [[nodiscard]] std::string Env(const char* name) {
            const char* value = std::getenv(name);
            return value != nullptr ? value : "";
        }

        [[nodiscard]] eastl::vector<uint8_t> ReadFile(const std::string& path) {
            std::ifstream file(path, std::ios::binary);
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            return eastl::vector<uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), reinterpret_cast<const uint8_t*>(bytes.data()) + bytes.size());
        }

        [[nodiscard]] bool Contains(const String& text, const char* part) {
            return text.find(part) != String::npos;
        }

        class TlsLocalEndpointTest : public ::testing::Test {
        protected:
            /// Skips the case when the servers are not there or this build has no backend
            void SetUp() override {
                if (!IsTlsSupported()) {
                    GTEST_SKIP() << "no TLS backend in this build";
                }
                if (Env(kEnvPort).empty() || Env(kEnvCaFile).empty()) {
                    GTEST_SKIP() << "no local TLS servers: run it through tls_local_check.py (ctest BagiTlsLocalEndpoint)";
                }
            }

            [[nodiscard]] static String Url(const char* host, const char* portVariable) {
                return Format("wss://{}:{}", host, Env(portVariable));
            }

            [[nodiscard]] static WebSocketClientSettings SettingsTrustingTheTestCa() {
                WebSocketClientSettings settings;
                settings.tls.additionalTrustAnchorsDer.push_back(ReadFile(Env(kEnvCaFile)));
                return settings;
            }

            void Pump() {
                WebSocketClientEvent event;
                while (client.PollEvent(event)) {
                    events.push_back(std::move(event));
                }
            }

            [[nodiscard]] const WebSocketClientEvent* Find(WebSocketClientEventKind kind) const {
                for (const WebSocketClientEvent& event : events) {
                    if (event.kind == kind) {
                        return &event;
                    }
                }
                return nullptr;
            }

            bool WaitFor(WebSocketClientEventKind kind) {
                return WaitUntil([this] { Pump(); }, [&] { return Find(kind) != nullptr; });
            }

            /// The one Closed event of a connection that has to be refused, and nothing that says it was ever open
            const WebSocketClientEvent& ExpectRefused() {
                EXPECT_TRUE(WaitFor(WebSocketClientEventKind::Closed)) << "the connection never ended";
                EXPECT_EQ(Find(WebSocketClientEventKind::Opened), nullptr);
                const WebSocketClientEvent* closed = Find(WebSocketClientEventKind::Closed);
                static const WebSocketClientEvent none;
                if (closed == nullptr) {
                    return none;
                }
                EXPECT_EQ(closed->closeCode, WebSocketCloseCode::kAbnormalClosure);
                return *closed;
            }

            WebSocketClient client;
            std::vector<WebSocketClientEvent> events;
        };

    }  // namespace

    TEST_F(TlsLocalEndpointTest, ATrustedCertificateOpensAndEchoesText) {
        ASSERT_TRUE(client.Open(Url("localhost", kEnvPort), SettingsTrustingTheTestCa()).has_value());
        ASSERT_TRUE(WaitFor(WebSocketClientEventKind::Opened)) << (Find(WebSocketClientEventKind::Closed) != nullptr ? Find(WebSocketClientEventKind::Closed)->reason.c_str() : "no event");

        ASSERT_TRUE(client.SendText("hello over tls"));
        const std::string large(kLargeMessageBytes, 'x');
        ASSERT_TRUE(client.SendText(String(large.c_str(), large.size())));
        ASSERT_TRUE(WaitUntil([this] { Pump(); }, [&] { return events.size() >= 3; }));
        EXPECT_EQ(events[1].kind, WebSocketClientEventKind::Text);
        EXPECT_EQ(events[1].text, "hello over tls");
        EXPECT_EQ(events[2].kind, WebSocketClientEventKind::Text);
        EXPECT_EQ(events[2].text.size(), kLargeMessageBytes);

        client.Close(1000, "done");
        ASSERT_TRUE(WaitFor(WebSocketClientEventKind::Closed));
        EXPECT_EQ(Find(WebSocketClientEventKind::Closed)->closeCode, 1000u);
    }

    TEST_F(TlsLocalEndpointTest, ACertificateOfAnUnknownCaIsRefusedWithoutTheAnchor) {
        // The check is the system's: the test CA is nobody's, so without the extra anchor the connection must not open
        ASSERT_TRUE(client.Open(Url("localhost", kEnvPort)).has_value());
        const WebSocketClientEvent& closed = ExpectRefused();
        EXPECT_TRUE(Contains(closed.reason, "certificate")) << closed.reason.c_str();
    }

    TEST_F(TlsLocalEndpointTest, AWrongHostNameIsRefusedEvenWithTheAnchor) {
        // The certificate is for DNS:localhost; 127.0.0.1 is not that name
        ASSERT_TRUE(client.Open(Url("127.0.0.1", kEnvPort), SettingsTrustingTheTestCa()).has_value());
        const WebSocketClientEvent& closed = ExpectRefused();
        EXPECT_TRUE(Contains(closed.reason, "host name") || Contains(closed.reason, "certificate")) << closed.reason.c_str();
    }

    TEST_F(TlsLocalEndpointTest, AnExpiredCertificateIsRefusedEvenWithTheAnchor) {
        if (Env(kEnvExpiredPort).empty()) {
            GTEST_SKIP() << "no server with an expired certificate";
        }
        ASSERT_TRUE(client.Open(Url("localhost", kEnvExpiredPort), SettingsTrustingTheTestCa()).has_value());
        const WebSocketClientEvent& closed = ExpectRefused();
        EXPECT_TRUE(Contains(closed.reason, "expired")) << closed.reason.c_str();
    }

    TEST_F(TlsLocalEndpointTest, APlainServerThatNeverAnswersEndsAtTheHandshakeDeadline) {
        if (Env(kEnvPlainPort).empty()) {
            GTEST_SKIP() << "no plain server";
        }
        WebSocketClientSettings settings;
        settings.handshakeTimeout = kShortHandshake;
        const auto started = std::chrono::steady_clock::now();
        ASSERT_TRUE(client.Open(Url("127.0.0.1", kEnvPlainPort), std::move(settings)).has_value());
        const WebSocketClientEvent& closed = ExpectRefused();
        EXPECT_LT(std::chrono::steady_clock::now() - started, kShortHandshake + kHandshakeSlack);
        EXPECT_TRUE(Contains(closed.reason, "TLS")) << closed.reason.c_str();
    }

    TEST_F(TlsLocalEndpointTest, ShutdownBothWakesAReceiveThatWaitsOnTheRealBackend) {
        auto stream = MakeTlsStream();
        TlsConnectParams params;
        params.host = "localhost";
        params.port = static_cast<uint16_t>(std::stoi(Env(kEnvPort)));
        params.handshakeDeadline = std::chrono::steady_clock::now() + kClientWaitLimit;
        const auto trusted = ReadFile(Env(kEnvCaFile));
        params.additionalTrustAnchorsDer = eastl::span<const eastl::vector<uint8_t>>(&trusted, 1);
        ASSERT_TRUE(stream->Connect(params).has_value()) << stream->GetLastError().c_str();
        ASSERT_TRUE(stream->IsValid());

        std::atomic<bool> returned{false};
        std::expected<size_t, SocketError> result = 0;
        std::thread reader([&] {
            std::array<uint8_t, 16> buffer{};
            result = stream->Receive(buffer);  // the server says nothing: this waits
            returned = true;
        });
        std::this_thread::sleep_for(kShortPause);
        EXPECT_FALSE(returned);
        stream->ShutdownBoth();
        EXPECT_TRUE(WaitUntil({}, [&] { return returned.load(); }, kHandshakeSlack)) << "ShutdownBoth() did not wake Receive()";
        reader.join();
        ASSERT_FALSE(result.has_value());
        EXPECT_EQ(result.error(), SocketError::Closed);
        EXPECT_FALSE(stream->SendAll(AsBytes(std::string("x"))));
        stream->Close();
        EXPECT_FALSE(stream->IsValid());
    }

    TEST_F(TlsLocalEndpointTest, ReceiveTimesOutAndKeepsTheStreamUsable) {
        auto stream = MakeTlsStream();
        TlsConnectParams params;
        params.host = "localhost";
        params.port = static_cast<uint16_t>(std::stoi(Env(kEnvPort)));
        params.handshakeDeadline = std::chrono::steady_clock::now() + kClientWaitLimit;
        const auto trusted = ReadFile(Env(kEnvCaFile));
        params.additionalTrustAnchorsDer = eastl::span<const eastl::vector<uint8_t>>(&trusted, 1);
        ASSERT_TRUE(stream->Connect(params).has_value()) << stream->GetLastError().c_str();
        ASSERT_TRUE(stream->SetReceiveTimeout(kShortPause));
        std::array<uint8_t, 16> buffer{};
        for (int attempt = 0; attempt < 2; ++attempt) {
            const auto result = stream->Receive(buffer);
            ASSERT_FALSE(result.has_value());
            EXPECT_EQ(result.error(), SocketError::TimedOut);
        }
        EXPECT_TRUE(stream->IsValid());
    }

    TEST_F(TlsLocalEndpointTest, CloseDuringTheHandshakeEndsConnect) {
        if (Env(kEnvPlainPort).empty()) {
            GTEST_SKIP() << "no plain server";
        }
        auto stream = MakeTlsStream();
        TlsConnectParams params;
        params.host = "127.0.0.1";
        params.port = static_cast<uint16_t>(std::stoi(Env(kEnvPlainPort)));
        params.handshakeDeadline = std::chrono::steady_clock::now() + kClientWaitLimit;  // the server never answers: only Close() can end it in time
        std::atomic<bool> returned{false};
        std::expected<void, String> result;
        std::thread connecting([&] {
            result = stream->Connect(params);
            returned = true;
        });
        std::this_thread::sleep_for(kShortPause);
        EXPECT_FALSE(returned);
        stream->Close();
        EXPECT_TRUE(WaitUntil({}, [&] { return returned.load(); }, kHandshakeSlack)) << "Close() did not end Connect()";
        connecting.join();
        EXPECT_FALSE(result.has_value());
        EXPECT_FALSE(stream->IsValid());
    }

    TEST_F(TlsLocalEndpointTest, CloseRacingConnectIsSafeAtEveryMoment) {
        constexpr int kAttempts = 150;
        constexpr int kDelaySteps = 12;
        constexpr std::chrono::microseconds kDelayStep{40};
        const auto trusted = ReadFile(Env(kEnvCaFile));
        int connected = 0;
        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            auto stream = MakeTlsStream();
            TlsConnectParams params;
            params.host = "localhost";
            params.port = static_cast<uint16_t>(std::stoi(Env(kEnvPort)));
            params.handshakeDeadline = std::chrono::steady_clock::now() + kClientWaitLimit;
            params.additionalTrustAnchorsDer = eastl::span<const eastl::vector<uint8_t>>(&trusted, 1);
            std::atomic<bool> started{false};
            std::expected<void, String> result;
            std::thread connecting([&] {
                started = true;
                result = stream->Connect(params);
            });
            while (!started) {
                std::this_thread::yield();
            }
            std::this_thread::sleep_for(kDelayStep * (attempt % kDelaySteps));  // the close lands at every point from before the connect to after it
            stream->Close();
            connecting.join();  // whatever the moment, Connect() ends (a win or a cancel) and nothing is touched after it was freed
            if (result.has_value()) {
                ++connected;
            }
            EXPECT_FALSE(stream->IsValid());
        }
        EXPECT_LT(connected, kAttempts) << "no Close() ever ended a Connect(): the test did not hit the window";
    }

}  // namespace BECore::Tests
