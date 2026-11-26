#include <Application/ControlLink.h>
#include <BECore/Control/ControlActivity.h>
#include <BECore/Control/ControlBuiltins.h>
#include <BECore/Control/ControlCall.h>
#include <BECore/Control/ControlParams.h>
#include <BECore/Control/JsonWriter.h>
#include <BECore/Logger/LogEvent.h>
#include <BECore/Rooms/FakeRoomTransport.h>
#include <Common/LogRecorder.h>
#include <Common/Printers.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;

        constexpr StringView kToken = "secret_Token-1";

        [[nodiscard]] Optional<ControlLinkSettings> Parsed(StringView query) {
            const auto parsed = ParseControlLinkQuery(query);
            EXPECT_TRUE(parsed.has_value()) << String(query).c_str();
            return parsed ? *parsed : Optional<ControlLinkSettings>();
        }

    }  // namespace

    TEST(ControlLinkQueryTest, DialsOnlyWithAToken) {
        EXPECT_FALSE(Parsed("").has_value());
        EXPECT_FALSE(Parsed("?fullscreen=0&token=abc").has_value());

        const Optional<ControlLinkSettings> bare = Parsed("?controlToken=abc");
        ASSERT_TRUE(bare.has_value());
        EXPECT_EQ(bare->token, "abc");
        EXPECT_EQ(bare->address, "ws://localhost:8766");
        EXPECT_FALSE(bare->manualClock);

        const Optional<ControlLinkSettings> full = Parsed("?fullscreen=0&controlClock=manual&controlToken=abc&control=9000&controlToken=other");
        ASSERT_TRUE(full.has_value());
        EXPECT_EQ(full->token, "abc");
        EXPECT_EQ(full->address, "ws://localhost:9000");
        EXPECT_TRUE(full->manualClock);
    }

    TEST(ControlLinkQueryTest, TakesAnAddressOfTheLoopback) {
        for (const StringView address : {"ws://localhost", "ws://localhost:1/", "ws://127.0.0.1:8766", "ws://127.0.0.1/path", "ws://[::1]:65535", "ws://[::1]"}) {
            const Optional<ControlLinkSettings> settings = Parsed(Format("controlToken=abc&control={}", address));
            ASSERT_TRUE(settings.has_value()) << String(address).c_str();
            EXPECT_EQ(settings->address, address);
        }
    }

    // Anyone can craft a link to a hosted page: it must not hand the game to another machine
    TEST(ControlLinkQueryTest, RefusesEveryOtherAddress) {
        for (const StringView address : {"ws://example.com:8766",
                                         "ws://localhost.example.com",
                                         "ws://127.0.0.1.example.com",
                                         "ws://localhost@example.com",
                                         "ws://localhost:80@example.com",
                                         "ws://example.com/ws://localhost",
                                         "ws://localhost\\@example.com",
                                         "ws://local\thost",
                                         "ws://localhost:8766#x",
                                         "ws://localhost:",
                                         "ws://localhost:0",
                                         "ws://localhost:65536",
                                         "ws://0.0.0.0",
                                         "ws://127.0.0.2",
                                         "ws://[::2]",
                                         "ws://::1",
                                         "wss://localhost",
                                         "http://localhost",
                                         "//localhost",
                                         "localhost:8766",
                                         "ws://",
                                         "0",
                                         "65536",
                                         ""}) {
            EXPECT_FALSE(ParseControlLinkQuery(Format("controlToken=abc&control={}", address)).has_value()) << String(address).c_str();
        }
    }

    TEST(ControlLinkQueryTest, RefusesWhatCannotBeMeant) {
        const String longest(128, 'a');
        EXPECT_TRUE(Parsed(Format("controlToken={}", longest)).has_value());
        for (const String& query : {Format("controlToken={}a", longest), String("controlToken="), String("controlToken"), String("controlToken=a%20b"), String("control=8766"),
                                    String("controlClock=manual"), String("controlToken=abc&controlClock=real"), String("controlToken=abc&controlClock"), String("controlToken=abc&control")}) {
            EXPECT_FALSE(ParseControlLinkQuery(query).has_value()) << query.c_str();
        }
    }

    TEST(ControlLinkQueryTest, AnErrorNeverHoldsTheToken) {
        for (const StringView query : {"controlToken=hush.hush", "controlToken=hush&control=ws://hush.example.com", "controlToken=hush&controlClock=hush"}) {
            const auto parsed = ParseControlLinkQuery(query);
            ASSERT_FALSE(parsed.has_value()) << String(query).c_str();
            EXPECT_EQ(parsed.error().find("hush"), String::npos) << parsed.error().c_str();
        }
    }

    class ControlLinkTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _service.RegisterHandler("count", [this](ControlCall& call) { call.Reply([this](JsonWriter& writer) { writer.Value(++_handled); }); });
            _service.RegisterHandler("later", [](ControlCall& call) { call.DeferFrames(2, [](ControlCall& done) { done.Reply([](JsonWriter& writer) { writer.Value(true); }); }); });
            ControlLinkSettings settings;
            settings.address = "ws://localhost:8766";
            settings.token = String(kToken);
            _link = MakeUnique<ControlLink>(_service, std::move(settings), _transports.Factory());
        }

        void Poll() {
            _link->Poll(_now);
        }

        /// Lets @p time pass, then polls
        void PollAfter(milliseconds time) {
            _now += time;
            Poll();
        }

        /// Brings the connection that is being made, or the first one, up
        void Open() {
            Poll();
            _transports.Last().PushOpened();
            Poll();
        }

        void Receive(StringView message) {
            _transports.Last().PushText(message);
            Poll();
        }

        /// The ids of the clients the journal says have gone, in that order
        [[nodiscard]] Vector<uint64_t> Disconnected() {
            Vector<uint64_t> ids;
            const ControlActivity& activity = _service.Activity();
            for (size_t i = 0; i < activity.Size(); ++i) {
                if (activity.At(i).kind == ControlActivityKind::Disconnected) {
                    ids.push_back(activity.At(i).client->GetId());
                }
            }
            return ids;
        }

        /// Whether a push reaches anything: the writer of a push runs only when somebody listens
        [[nodiscard]] bool IsListening() {
            bool written = false;
            _service.Publish("probe", [&written](JsonWriter& writer) {
                written = true;
                writer.Null();
            });
            return written;
        }

        // Declared first: the service outlives the link
        ControlService _service;
        FakeTransports _transports;
        UniquePtr<ControlLink> _link;
        ControlLinkTime _now{};
        int _handled = 0;
    };

    TEST_F(ControlLinkTest, SaysHelloBeforeAnythingElse) {
        Poll();
        ASSERT_EQ(_transports.Count(), 1u);
        EXPECT_EQ(_transports.Last().url, "ws://localhost:8766");
        EXPECT_TRUE(_transports.Last().sent.empty());
        EXPECT_FALSE(IsListening());

        _transports.Last().PushOpened();
        Poll();
        ASSERT_EQ(_transports.Last().sent.size(), 1u);
        EXPECT_EQ(_transports.Last().sent[0], R"({"bagiControlLink":1,"token":"secret_Token-1"})");
    }

    TEST_F(ControlLinkTest, AnswersOnTheChannelOfTheRequest) {
        Open();
        Receive(R"(7 {"id":1,"method":"count"})");
        Receive(R"(12 {"id":1,"method":"count"})");
        _service.Drain();

        const Vector<String>& sent = _transports.Last().sent;
        ASSERT_EQ(sent.size(), 3u);
        EXPECT_EQ(sent[1], R"(7 {"id":1,"ok":true,"result":1})");
        EXPECT_EQ(sent[2], R"(12 {"id":1,"ok":true,"result":2})");
    }

    TEST_F(ControlLinkTest, AnswersADeferredCallWhenItsFramesHavePassed) {
        Open();
        Receive(R"(3 {"id":"a","method":"later"})");
        _service.Drain();
        _service.EndFrame();
        ASSERT_EQ(_transports.Last().sent.size(), 1u);

        _service.EndFrame();
        ASSERT_EQ(_transports.Last().sent.size(), 2u);
        EXPECT_EQ(_transports.Last().sent[1], R"(3 {"id":"a","ok":true,"result":true})");
    }

    TEST_F(ControlLinkTest, SendsAPushToEveryClient) {
        Open();
        _service.Publish("state_changed", [](JsonWriter& writer) { writer.Value(5); });

        ASSERT_EQ(_transports.Last().sent.size(), 2u);
        EXPECT_EQ(_transports.Last().sent[1], R"(0 {"data":5,"event":"state_changed"})");
    }

    // Without a client of its own `client_hello` answers `no_client`, and the editor shows nobody
    TEST_F(ControlLinkTest, AChannelIsAClientUntilItsNoticeOrTheEndOfTheConnection) {
        RegisterControlBuiltins(_service);
        Open();
        Receive(R"(7 {"id":1,"method":"client_hello","params":{"name":"mcp"}})");
        Receive(R"(9 {"id":1,"method":"client_hello","params":{"name":"artgen"}})");
        Receive(R"(7 {"id":2,"method":"client_hello","params":{"name":"qa"}})");
        _service.Drain();

        const Vector<String>& sent = _transports.Last().sent;
        ASSERT_EQ(sent.size(), 4u);
        EXPECT_EQ(sent[1], R"(7 {"id":1,"ok":true,"result":{"id":1,"name":"mcp"}})");
        EXPECT_EQ(sent[2], R"(9 {"id":1,"ok":true,"result":{"id":2,"name":"artgen"}})");
        EXPECT_EQ(sent[3], R"(7 {"id":2,"ok":true,"result":{"id":1,"name":"qa"}})");

        Receive("7");
        _service.Drain();
        EXPECT_EQ(Disconnected(), Vector<uint64_t>{1});

        _transports.Last().PushClosed();
        Poll();
        _service.Drain();
        EXPECT_EQ(Disconnected(), (Vector<uint64_t>{1, 2}));
    }

    TEST_F(ControlLinkTest, IgnoresTheNoticeThatAnUnknownChannelLeft) {
        Open();
        Receive("4");
        Receive(R"(5 {"id":1,"method":"count"})");
        _service.Drain();

        EXPECT_FALSE(_transports.Last().aborted);
        ASSERT_EQ(_transports.Last().sent.size(), 2u);
        EXPECT_EQ(_transports.Last().sent[1], R"(5 {"id":1,"ok":true,"result":1})");
    }

    TEST_F(ControlLinkTest, AbortsOnAMessageWithoutAChannel) {
        for (const StringView message : {R"({"id":1,"method":"count"})", R"(x {"id":1,"method":"count"})", R"(0 {"id":1,"method":"count"})", R"( {"id":1,"method":"count"})", R"(-1)", ""}) {
            Open();
            FakeLink& link = _transports.Last();
            link.PushText(message);
            link.PushText(R"(1 {"id":1,"method":"count"})");  // arrived before the abort: no answer could go back, so it must not run
            Poll();
            _service.Drain();

            EXPECT_TRUE(link.aborted) << String(message).c_str();
            EXPECT_EQ(link.sent.size(), 1u) << String(message).c_str();
            EXPECT_TRUE(link.destroyed);
            _now += milliseconds(1000);
        }
        EXPECT_EQ(_handled, 0);
    }

    // The bridge passes a client's text unparsed, and the browser transport cannot send a NUL: the answer to such a text must not hold one
    TEST_F(ControlLinkTest, AnswersARequestWithARawNulInATextWithoutOne) {
        Open();
        constexpr char kInAString[] = "1 {\"id\":\"a\0b\",\"method\":\"count\"}";
        constexpr char kBehindTheObject[] = "1 {\"id\":1,\"method\":\"count\"}\0";
        Receive(StringView(kInAString, sizeof(kInAString) - 1));
        Receive(StringView(kBehindTheObject, sizeof(kBehindTheObject) - 1));
        _service.Drain();

        const Vector<String>& sent = _transports.Last().sent;
        ASSERT_EQ(sent.size(), 3u);
        for (size_t i = 1; i < sent.size(); ++i) {
            EXPECT_EQ(sent[i].find('\0'), String::npos);
            EXPECT_NE(sent[i].find("parse_error"), String::npos) << sent[i].c_str();
        }
        EXPECT_EQ(_handled, 0);
    }

    TEST_F(ControlLinkTest, DropsTheReplyOfAnEarlierConnection) {
        Open();
        Receive(R"(1 {"id":1,"method":"count"})");
        FakeLink& first = _transports.Last();
        first.PushClosed();
        Poll();
        PollAfter(milliseconds(1000));
        ASSERT_EQ(_transports.Count(), 2u);
        _transports.Last().PushOpened();
        Poll();

        _service.Drain();  // the request of the first connection runs now: channel 1 of this one is another client
        EXPECT_EQ(_handled, 1);
        EXPECT_EQ(first.sent.size(), 1u);
        EXPECT_EQ(_transports.Last().sent.size(), 1u);
    }

    TEST_F(ControlLinkTest, DropsTheRepliesThatComeAfterStop) {
        Open();
        Receive(R"(1 {"id":1,"method":"later"})");
        _service.Drain();
        Receive(R"(2 {"id":2,"method":"count"})");

        _link->Stop();
        EXPECT_TRUE(_transports.Last().destroyed);
        EXPECT_FALSE(IsListening());
        _service.Shutdown();
        EXPECT_EQ(_transports.Last().sent.size(), 1u);

        PollAfter(std::chrono::hours(1));
        EXPECT_EQ(_transports.Count(), 1u);
    }

    TEST_F(ControlLinkTest, DropsTheRepliesThatComeAfterItsDestruction) {
        Open();
        Receive(R"(1 {"id":1,"method":"later"})");
        _service.Drain();
        Receive(R"(2 {"id":2,"method":"count"})");

        _link.reset();
        EXPECT_TRUE(_transports.Last().destroyed);
        EXPECT_FALSE(IsListening());
        _service.Shutdown();
        EXPECT_EQ(_transports.Last().sent.size(), 1u);
    }

    TEST_F(ControlLinkTest, RetriesWithAGrowingDelayAndStartsOverAfterAConnectionThatOpened) {
        Poll();
        size_t connections = 1;
        for (const int seconds : {1, 2, 4, 8, 10, 10}) {
            _transports.Last().PushClosed();
            Poll();
            PollAfter(milliseconds(seconds * 1000 - 1));
            ASSERT_EQ(_transports.Count(), connections) << seconds;
            PollAfter(milliseconds(1));
            ASSERT_EQ(_transports.Count(), ++connections) << seconds;
        }

        _transports.Last().PushOpened();
        _transports.Last().PushClosed(1001, "going away");
        Poll();
        PollAfter(milliseconds(1000));
        EXPECT_EQ(_transports.Count(), connections + 1);
    }

    TEST_F(ControlLinkTest, RetriesAConnectionThatCannotStart) {
        _transports.FailNextOpen("refused");
        Poll();
        Poll();
        ASSERT_EQ(_transports.Count(), 1u);
        PollAfter(milliseconds(1000));
        EXPECT_EQ(_transports.Count(), 2u);
    }

    TEST_F(ControlLinkTest, StopsWhenTheBridgeRefusesItAndSaysWhyOnce) {
        const LogRecorder log;
        Open();
        _transports.Last().PushClosed(kRoomTransportPolicyViolation, "bad_token");
        Poll();
        PollAfter(std::chrono::hours(1));
        EXPECT_EQ(_transports.Count(), 1u);

        LogEvent::Flush();
        EXPECT_EQ(log.Count(LogLevel::Warning, {"bad_token"}), 1u);
        for (const LogLevel level : {LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error}) {
            EXPECT_EQ(log.Count(level, {kToken}), 0u);
        }
    }

    // A QA run reads every warning as a finding, and a bridge that is not running is none
    TEST_F(ControlLinkTest, ALostLinkIsNoWarning) {
        const LogRecorder log;
        Open();
        _transports.Last().PushClosed();
        Poll();
        LogEvent::Flush();
        EXPECT_EQ(log.Count(LogLevel::Warning, {}), 0u);
        EXPECT_EQ(log.Count(LogLevel::Error, {}), 0u);
    }

    TEST_F(ControlLinkTest, ARefusedSendAbortsTheLinkAndRetries) {
        Open();
        Receive(R"(1 {"id":1,"method":"count"})");
        _transports.Last().sendOk = false;
        _service.Drain();

        EXPECT_TRUE(_transports.Last().aborted);
        EXPECT_FALSE(IsListening());
        Poll();
        PollAfter(milliseconds(1000));
        EXPECT_EQ(_transports.Count(), 2u);
    }

    TEST_F(ControlLinkTest, ARefusedPushAbortsTheLink) {
        Open();
        _transports.Last().sendOk = false;
        EXPECT_TRUE(IsListening());  // this push is the refused one
        EXPECT_TRUE(_transports.Last().aborted);
        EXPECT_FALSE(IsListening());
    }

    TEST_F(ControlLinkTest, ARefusedHelloAbortsTheLink) {
        Poll();
        _transports.Last().sendOk = false;
        _transports.Last().PushOpened();
        Poll();
        EXPECT_TRUE(_transports.Last().aborted);
        EXPECT_FALSE(IsListening());
        PollAfter(milliseconds(1000));
        EXPECT_EQ(_transports.Count(), 2u);
    }

    TEST_F(ControlLinkTest, RefusesTheMethodsThatUseFilesOfTheHost) {
        const Array<StringView, 3> methods = {"record_start", "replay", "profiler_dump"};
        for (const StringView method : methods) {
            _service.RegisterHandler(method, [this](ControlCall& call) { call.Reply([this](JsonWriter& writer) { writer.Value(++_handled); }); });
        }
        RefuseHostFileMethods(_service);
        Open();
        for (const StringView method : methods) {
            Receive(Format(R"(1 {{"id":1,"method":"{}","params":{{"path":"a.jsonl"}}}})", method));
        }
        Receive(R"(1 {"id":2,"method":"count"})");
        _service.Drain();

        const Vector<String>& sent = _transports.Last().sent;
        ASSERT_EQ(sent.size(), methods.size() + 2);
        for (size_t i = 1; i <= methods.size(); ++i) {
            EXPECT_NE(sent[i].find(R"("code":"unsupported")"), String::npos) << sent[i].c_str();
        }
        EXPECT_EQ(sent.back(), R"(1 {"id":2,"ok":true,"result":1})");  // no handler of the three ran
    }

}  // namespace BECore::Tests
