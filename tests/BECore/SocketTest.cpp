#include <RemoteControl/Socket.h>
#include <gtest/gtest.h>
#include <thread>

namespace BECore::Tests {

    namespace {

        using namespace std::chrono_literals;

        constexpr auto kWaitLimit = 10s;

        /// A connected pair: the client end and the end the server accepted
        struct SocketPair {
            Socket client;
            Socket accepted;
        };

        void MakePair(SocketPair& pair) {
            auto listener = Socket::Listen(0);
            ASSERT_TRUE(listener.has_value());
            auto client = Socket::Connect(listener->GetLocalPort());
            ASSERT_TRUE(client.has_value());
            auto accepted = listener->Accept(kWaitLimit);
            ASSERT_TRUE(accepted.has_value());
            pair.client = std::move(*client);
            pair.accepted = std::move(*accepted);
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

    // Winsock does not wake a blocked recv() on shutdown(), so Windows waits in slices (Socket.h)
    TEST(SocketTest, ShutdownBothWakesAReceiveThatWaits) {
        SocketPair pair;
        ASSERT_NO_FATAL_FAILURE(MakePair(pair));

        std::atomic<bool> ended{false};
        std::atomic<bool> closed{false};
        std::thread reader([&] {
            std::array<uint8_t, 16> buffer;
            const auto received = pair.accepted.Receive(Span<uint8_t>(buffer.data(), buffer.size()));
            closed = !received && received.error() == SocketError::Closed;
            ended = true;
        });

        std::this_thread::sleep_for(100ms);  // let the reader block; the peer sends nothing and stays open
        EXPECT_FALSE(ended.load());
        pair.accepted.ShutdownBoth();

        const bool woke = WaitUntil([&] { return ended.load(); });
        if (!woke) {
            pair.client.Close();  // let the test end even when the wake-up is broken
        }
        reader.join();
        EXPECT_TRUE(woke) << "Receive() was not woken by ShutdownBoth()";
        EXPECT_TRUE(closed.load());
    }

    TEST(SocketTest, ShutdownBothWakesASendThatWaitsForTheReader) {
        SocketPair pair;
        ASSERT_NO_FATAL_FAILURE(MakePair(pair));

        // The peer never reads, so the buffers fill up and the send has to wait
        std::atomic<bool> ended{false};
        std::atomic<bool> sent{true};
        std::thread writer([&] {
            const Vector<uint8_t> block(64 * 1024, 0x5A);
            for (int i = 0; i < 4096 && sent; ++i) {
                sent = pair.accepted.SendAll(Span<const uint8_t>(block.data(), block.size()));
            }
            ended = true;
        });

        std::this_thread::sleep_for(300ms);
        EXPECT_FALSE(ended.load());
        pair.accepted.ShutdownBoth();

        const bool woke = WaitUntil([&] { return ended.load(); });
        if (!woke) {
            pair.client.Close();
        }
        writer.join();
        EXPECT_TRUE(woke) << "SendAll() was not woken by ShutdownBoth()";
        EXPECT_FALSE(sent.load());
    }

    TEST(SocketTest, ReceiveTimesOutWhenNothingComes) {
        SocketPair pair;
        ASSERT_NO_FATAL_FAILURE(MakePair(pair));

        ASSERT_TRUE(pair.accepted.SetReceiveTimeout(80ms));
        std::array<uint8_t, 16> buffer;
        const auto before = std::chrono::steady_clock::now();
        const auto received = pair.accepted.Receive(Span<uint8_t>(buffer.data(), buffer.size()));
        const auto waited = std::chrono::steady_clock::now() - before;

        ASSERT_FALSE(received.has_value());
        EXPECT_EQ(received.error(), SocketError::TimedOut);
        EXPECT_GE(waited, 60ms);
        EXPECT_LT(waited, 5s);
    }

    TEST(SocketTest, ReceiveReturnsWhatArrivedAndSeesTheEndOfTheStream) {
        SocketPair pair;
        ASSERT_NO_FATAL_FAILURE(MakePair(pair));

        const std::array<uint8_t, 3> bytes = {1, 2, 3};
        ASSERT_TRUE(pair.client.SendAll(Span<const uint8_t>(bytes.data(), bytes.size())));
        std::array<uint8_t, 16> buffer;
        const auto received = pair.accepted.Receive(Span<uint8_t>(buffer.data(), buffer.size()));
        ASSERT_TRUE(received.has_value());
        EXPECT_EQ(*received, 3u);

        pair.client.Close();
        const auto ended = pair.accepted.Receive(Span<uint8_t>(buffer.data(), buffer.size()));
        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), SocketError::Closed);
    }

    TEST(SocketTest, AnyAddressListenerAcceptsALoopbackClientAndTellsItsAddress) {
        auto listener = Socket::Listen(0, SocketBind::AnyAddress);
        ASSERT_TRUE(listener.has_value());
        ASSERT_NE(listener->GetLocalPort(), 0u);
        auto client = Socket::Connect(listener->GetLocalPort());
        ASSERT_TRUE(client.has_value());

        String peerAddress;
        auto accepted = listener->Accept(kWaitLimit, &peerAddress);
        ASSERT_TRUE(accepted.has_value());
        EXPECT_EQ(peerAddress, "127.0.0.1");
        EXPECT_TRUE(accepted->IsValid());
    }

    TEST(SocketTest, DefaultListenerBindsTheLoopbackAndAcceptsWithoutAPeerAddress) {
        auto listener = Socket::Listen(0);
        ASSERT_TRUE(listener.has_value());
        ASSERT_NE(listener->GetLocalPort(), 0u);
        auto client = Socket::Connect(listener->GetLocalPort());
        ASSERT_TRUE(client.has_value());

        auto accepted = listener->Accept(kWaitLimit);  // peerAddress is optional
        ASSERT_TRUE(accepted.has_value());
        EXPECT_TRUE(accepted->IsValid());
    }

    TEST(SocketTest, ListenOnATakenPortNamesTheAddressInTheError) {
        auto first = Socket::Listen(0, SocketBind::AnyAddress);
        ASSERT_TRUE(first.has_value());
        const auto second = Socket::Listen(first->GetLocalPort(), SocketBind::AnyAddress);
        ASSERT_FALSE(second.has_value());
        EXPECT_NE(second.error().find("0.0.0.0"), String::npos);
    }

    TEST(SocketTest, ConnectByIPv4LiteralAndByNameReachesTheListener) {
        auto listener = Socket::Listen(0);
        ASSERT_TRUE(listener.has_value());
        const uint16_t port = listener->GetLocalPort();

        for (const char* host : {"127.0.0.1", "localhost"}) {
            auto client = Socket::Connect(host, port, kWaitLimit);
            ASSERT_TRUE(client.has_value()) << host << ": " << client.error().c_str();
            auto accepted = listener->Accept(kWaitLimit);
            ASSERT_TRUE(accepted.has_value()) << host;

            // The connected socket is the usual blocking one: bytes go through both ways
            const std::array<uint8_t, 2> bytes = {7, 9};
            ASSERT_TRUE(client->SendAll(Span<const uint8_t>(bytes.data(), bytes.size())));
            std::array<uint8_t, 8> buffer;
            const auto received = accepted->Receive(Span<uint8_t>(buffer.data(), buffer.size()));
            ASSERT_TRUE(received.has_value()) << host;
            EXPECT_EQ(*received, 2u);
        }
    }

    TEST(SocketTest, ConnectToAClosedPortNamesHostAndPort) {
        uint16_t closedPort = 0;
        {
            auto listener = Socket::Listen(0);
            ASSERT_TRUE(listener.has_value());
            closedPort = listener->GetLocalPort();
        }
        const auto client = Socket::Connect("127.0.0.1", closedPort, kWaitLimit);
        ASSERT_FALSE(client.has_value());
        EXPECT_NE(client.error().find(Format("127.0.0.1:{}", closedPort)), String::npos) << client.error().c_str();
    }

    TEST(SocketTest, ConnectToAnUnresolvableNameIsAnErrorThatNamesTheHost) {
        const auto client = Socket::Connect("nonexistent.invalid", 80, kWaitLimit);
        ASSERT_FALSE(client.has_value());
        EXPECT_NE(client.error().find("nonexistent.invalid"), String::npos) << client.error().c_str();
    }

    TEST(SocketTest, ConnectWithTheCancelFlagAlreadySetReturnsCancelledAndConnectsNothing) {
        auto listener = Socket::Listen(0);
        ASSERT_TRUE(listener.has_value());
        const std::atomic<bool> cancel{true};

        const auto client = Socket::Connect("127.0.0.1", listener->GetLocalPort(), kWaitLimit, &cancel);
        ASSERT_FALSE(client.has_value());
        EXPECT_NE(client.error().find("cancelled"), String::npos) << client.error().c_str();
        const auto accepted = listener->Accept(100ms);
        ASSERT_FALSE(accepted.has_value());
        EXPECT_EQ(accepted.error(), SocketError::TimedOut);
    }

    TEST(SocketTest, ConnectWithAClearCancelFlagStillConnects) {
        auto listener = Socket::Listen(0);
        ASSERT_TRUE(listener.has_value());
        const std::atomic<bool> cancel{false};

        const auto client = Socket::Connect("127.0.0.1", listener->GetLocalPort(), kWaitLimit, &cancel);
        EXPECT_TRUE(client.has_value());
    }

    TEST(SocketTest, ConnectToTheLoopbackByPortOnlyStillWorks) {
        auto listener = Socket::Listen(0);
        ASSERT_TRUE(listener.has_value());
        auto client = Socket::Connect(listener->GetLocalPort());
        ASSERT_TRUE(client.has_value());
        EXPECT_TRUE(listener->Accept(kWaitLimit).has_value());
    }

}  // namespace BECore::Tests
