// The editor's link to a running control endpoint, over the real methods through a transport in memory.

#include <BECore/Control/ControlCall.h>
#include <BECore/FakeControlEndpoint.h>
#include <BECore/Reflection/PropertyAccess.h>
#include <Common/AllocCheck.h>
#include <Common/EngineEnvironment.h>
#include <Common/LogRecorder.h>
#include <Common/NullPropertyVisitor.h>
#include <Common/Printers.h>
#include <Common/Wait.h>
#include <Widgets/RemoteEditor/RemoteSession.h>
#include <gtest/gtest.h>

#if BE_WITH_REMOTE_CONTROL
#include <RemoteControl/WebSocketServer.h>
#endif

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        namespace Timing = RemoteSessionTiming;

        constexpr StringView kRemoteNodes = R"(
<node name="Board" layer="2"><components>
  <component type="TransformComponent"/>
  <component type="QuadRendererComponent"/>
</components></node>
<node name="Two"><components>
  <component type="TransformComponent"/>
  <component type="TransformComponent"/>
</components></node>)";

        constexpr StringView kAddress = "ws://127.0.0.1:8765";

        /// The field `x` a walk shows
        class XReader final : public NullPropertyVisitor {
        public:
            using NullPropertyVisitor::Visit;

            bool Visit(StringView name, float& value) override {
                if (name == "x") {
                    x = value;
                }
                return false;
            }

            Optional<float> x;
        };

        class RemoteSessionTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                Start(FakeControlEndpoint::Methods::All);
            }

            void TearDown() override {
                _session.reset();
                _endpoint.reset();
            }

            void Start(FakeControlEndpoint::Methods methods) {
                _session.reset();
                _endpoint.reset();
                _endpoint = MakeUnique<FakeControlEndpoint>(kRemoteNodes, methods);
                _session = MakeUnique<RemoteSession>(_endpoint->Factory());
            }

            void Poll() {
                _session->Poll(_now);
            }

            void PollAfter(milliseconds time) {
                _now += time;
                Poll();
            }

            /// Sends what is due, lets the program answer and takes the answers
            void Exchange() {
                Poll();
                _endpoint->Serve();
                Poll();
            }

            void ExchangeAfter(milliseconds time) {
                _now += time;
                Exchange();
            }

            void Attach() {
                ASSERT_TRUE(_session->Attach(kAddress).has_value());
                Exchange();
                ASSERT_EQ(_session->GetState(), RemoteSessionState::Attached);
            }

            void AttachAndSelect(StringView node) {
                ASSERT_NO_FATAL_FAILURE(Attach());
                _session->Select(node);
                Exchange();
                ASSERT_FALSE(_session->GetComponents().empty());
            }

            [[nodiscard]] FakeControlConnection& Connection() {
                return _endpoint->Connection();
            }

            [[nodiscard]] size_t Sent() {
                return Connection().requests.size();
            }

            [[nodiscard]] size_t CountSent(StringView method) {
                size_t count = 0;
                for (const String& sent : Connection().Methods()) {
                    count += StringView(sent) == method ? 1 : 0;
                }
                return count;
            }

            /// The id of the last request of @p method, as its text
            [[nodiscard]] String IdOf(StringView method) {
                for (auto request = Connection().requests.rbegin(); request != Connection().requests.rend(); ++request) {
                    const ControlReply read{StringView(*request)};
                    if (StringView(StringOf(read.View(), "method")) == method) {
                        return read.IdText();
                    }
                }
                ADD_FAILURE() << "nothing sent for " << String(method).c_str();
                return "0";
            }

            void AnswerWith(StringView method, StringView members) {
                Connection().PushText(Format(R"({{"id":{},{}}})", IdOf(method), members));
            }

            /// The component @p index of the node at @p node of the program's scene
            [[nodiscard]] TypedRef Component(StringView node, size_t index) {
                return TypedRef::Of(*At(_endpoint->Node(node).GetComponents(), index));
            }

            void SetSceneX(StringView node, size_t index, double x) {
                ASSERT_TRUE(SetProperty(Component(node, index), "x", PropertyValue{x}).has_value());
            }

            [[nodiscard]] double SceneX(StringView node, size_t index) {
                const auto value = GetProperty(Component(node, index), "x");
                return Require(value.has_value() ? std::get_if<double>(&*value) : nullptr);
            }

            /// The `x` an inspector would show for the component @p index of the selection
            [[nodiscard]] float ShownX(size_t index) {
                XReader reader;
                const Span<RemoteComponent> components = _session->GetComponents();
                if (index < components.size()) {
                    components[index].Walk(_session->GetSchema(), _session->GetWrites(), reader);
                }
                EXPECT_TRUE(reader.x.has_value()) << "component " << index << " shows no x";
                return reader.x.value_or(0.0f);
            }

            UniquePtr<FakeControlEndpoint> _endpoint;
            UniquePtr<RemoteSession> _session;
            RemoteSessionTime _now{};
        };

    }  // namespace

    TEST_F(RemoteSessionTest, AttachingReadsTheSchemaAndTheTree) {
        ASSERT_TRUE(_session->Attach(kAddress).has_value());
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Connecting);
        EXPECT_EQ(_session->GetAddress(), kAddress);

        Poll();
        EXPECT_EQ(Connection().Methods(), (Vector<String>{"status", "schema", "scene_tree"}));
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Connecting);
        _endpoint->Serve();
        Poll();

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_NE(_session->GetSchema().FindType("TransformComponent"), nullptr);
        EXPECT_NE(_session->GetTree().Find("Board"), nullptr);
        EXPECT_TRUE(_session->GetComponents().empty());
    }

    TEST_F(RemoteSessionTest, TheTreeIsAskedAgainOnlyAfterAnAnswerAndASilentProgramIsStalled) {
        ASSERT_NO_FATAL_FAILURE(Attach());
        const size_t attached = Sent();

        PollAfter(Timing::kTreeInterval - milliseconds(1));
        EXPECT_EQ(Sent(), attached);
        PollAfter(milliseconds(1));
        EXPECT_EQ(Connection().Methods(attached), (Vector<String>{"scene_tree"}));

        PollAfter(Timing::kStallAfter - milliseconds(1));
        EXPECT_FALSE(_session->IsStalled());
        PollAfter(milliseconds(1));
        EXPECT_TRUE(_session->IsStalled());
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_EQ(Sent(), attached + 1);

        _endpoint->Node("Board").SetLayer(7);
        _endpoint->Serve();
        Poll();

        EXPECT_FALSE(_session->IsStalled());
        EXPECT_EQ(Require(_session->GetTree().Find("Board")).layer, 7);
    }

    TEST_F(RemoteSessionTest, TheComponentsOfTheSelectedNodeAreKeptCurrent) {
        ASSERT_NO_FATAL_FAILURE(SetSceneX("Board", 0, 1.0));
        ASSERT_NO_FATAL_FAILURE(SetSceneX("Two", 0, 3.0));
        ASSERT_NO_FATAL_FAILURE(SetSceneX("Two", 1, 4.0));
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        ASSERT_EQ(_session->GetComponents().size(), 2u);
        EXPECT_EQ(_session->GetComponents()[0].GetType(), "TransformComponent");
        EXPECT_EQ(_session->GetComponents()[1].GetType(), "QuadRendererComponent");
        EXPECT_EQ(ShownX(0), 1.0f);

        ASSERT_NO_FATAL_FAILURE(SetSceneX("Board", 0, 9.0));
        ExchangeAfter(Timing::kComponentInterval);
        EXPECT_EQ(ShownX(0), 9.0f);

        // An answer about the node selected before is not shown for the next one, which is read at once
        PollAfter(Timing::kComponentInterval);
        _session->Select("Two");
        EXPECT_TRUE(_session->GetComponents().empty());
        _endpoint->Serve();
        Poll();
        EXPECT_TRUE(_session->GetComponents().empty());
        Exchange();
        ASSERT_EQ(_session->GetComponents().size(), 2u);
        EXPECT_EQ(ShownX(0), 3.0f);
        EXPECT_EQ(ShownX(1), 4.0f);

        _session->Select("Nothing");
        EXPECT_FALSE(_session->GetSelection().has_value());
        EXPECT_TRUE(_session->GetComponents().empty());
    }

    TEST_F(RemoteSessionTest, TheSelectionGoesWithItsNode) {
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Two"));
        _endpoint->GetScene().GetRootNode().RemoveChild(&_endpoint->Node("Two"));

        ExchangeAfter(Timing::kTreeInterval);

        EXPECT_FALSE(_session->GetSelection().has_value());
        EXPECT_TRUE(_session->GetComponents().empty());

        _session->Select("Board");
        _endpoint->SetHasScene(false);
        ExchangeAfter(Timing::kTreeInterval);

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_TRUE(_session->GetTree().GetNodes().empty());
        EXPECT_FALSE(_session->GetSelection().has_value());
    }

    TEST_F(RemoteSessionTest, AWriteReachesTheSceneAndShowsUntilALaterRead) {
        ASSERT_NO_FATAL_FAILURE(SetSceneX("Board", 0, 1.0));
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        const RemoteWriteTarget x{"Board", "TransformComponent", 0, "x"};

        // A read goes out before the write: its answer still has the old value
        PollAfter(Timing::kComponentInterval);
        _session->GetWrites().Edit(x, PropertyValue{5.0}, RemoteEditKind::Value);
        Exchange();

        EXPECT_EQ(SceneX("Board", 0), 5.0);
        EXPECT_NE(_session->GetWrites().Find(x), nullptr);
        EXPECT_EQ(ShownX(0), 5.0f);

        ExchangeAfter(Timing::kComponentInterval);

        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
        EXPECT_EQ(ShownX(0), 5.0f);
    }

    TEST_F(RemoteSessionTest, OneWriteOfATargetIsUnansweredAtATime) {
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        const RemoteWriteTarget x{"Board", "TransformComponent", 0, "x"};

        _session->GetWrites().Edit(x, PropertyValue{6.0}, RemoteEditKind::Value);
        Poll();
        _session->GetWrites().Edit(x, PropertyValue{7.0}, RemoteEditKind::Value);
        _session->GetWrites().Edit({"Board", "TransformComponent", 0, "y"}, PropertyValue{8.0}, RemoteEditKind::Value);
        Poll();
        EXPECT_EQ(CountSent("scene_set"), 2u);  // x once, and y

        _endpoint->Serve();
        Poll();
        EXPECT_EQ(CountSent("scene_set"), 3u);
        _endpoint->Serve();
        Poll();

        EXPECT_EQ(SceneX("Board", 0), 7.0);
    }

    TEST_F(RemoteSessionTest, ALayerWriteReachesTheSceneAndGoesWithALaterTree) {
        ASSERT_NO_FATAL_FAILURE(Attach());

        _session->GetWrites().Edit({"Board"}, PropertyValue{int64_t{-3}}, RemoteEditKind::Value);
        Exchange();

        EXPECT_EQ(_endpoint->Node("Board").GetLayer(), -3);
        EXPECT_FALSE(_session->GetWrites().GetWrites().empty());

        ExchangeAfter(Timing::kTreeInterval);

        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
        EXPECT_EQ(Require(_session->GetTree().Find("Board")).layer, -3);
    }

    TEST_F(RemoteSessionTest, ARefusedWriteKeepsTheReason) {
        ASSERT_NO_FATAL_FAILURE(Attach());

        _session->GetWrites().Edit({"Board", "TransformComponent", 0, "x"}, PropertyValue{String("wide")}, RemoteEditKind::Value);
        Exchange();

        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
        const RemoteRefusal& refusal = Require(_session->GetWrites().GetRefusal());
        EXPECT_EQ(refusal.path, "x");
        EXPECT_EQ(refusal.code, "type_mismatch");
        EXPECT_FALSE(refusal.message.empty());
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
    }

    TEST_F(RemoteSessionTest, ALostLinkKeepsTheDataAndIsRetriedWithLongerWaits) {
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        LogRecorder log;

        _endpoint->SetRefusing(true);
        _endpoint->Drop(1001, "going away");
        Poll();

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Lost);
        EXPECT_EQ(_session->GetLossReason(), "1001 going away");
        EXPECT_TRUE(Connection().destroyed);
        EXPECT_NE(_session->GetTree().Find("Board"), nullptr);
        EXPECT_EQ(_session->GetComponents().size(), 2u);
        EXPECT_TRUE(_session->GetSelection().has_value());
        _session->GetWrites().Edit({"Board"}, PropertyValue{int64_t{9}}, RemoteEditKind::Value);

        for (const milliseconds wait : {Timing::kFirstRetry, Timing::kFirstRetry * 2, Timing::kFirstRetry * 4, Timing::kFirstRetry * 8, Timing::kMaxRetry, Timing::kMaxRetry}) {
            const size_t connections = _endpoint->GetConnectionCount();
            PollAfter(wait - milliseconds(1));
            EXPECT_EQ(_endpoint->GetConnectionCount(), connections) << wait.count();
            PollAfter(milliseconds(1));
            EXPECT_EQ(_endpoint->GetConnectionCount(), connections + 1) << wait.count();
        }
        // The reason of the outage is what began it, and the outage is one line
        EXPECT_EQ(_session->GetLossReason(), "1001 going away");
        LogEvent::Flush();
        EXPECT_EQ(log.Count(LogLevel::Info, {"no connection to"}), 1u);
        EXPECT_EQ(log.Count(LogLevel::Warning, {}), 0u);

        _endpoint->SetRefusing(false);
        PollAfter(Timing::kMaxRetry);
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Lost);
        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
        _endpoint->Serve();
        Poll();

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_TRUE(_session->GetLossReason().empty());
        EXPECT_EQ(Connection().Methods(), (Vector<String>{"status", "schema", "scene_tree", "batch"}));
        EXPECT_EQ(_endpoint->Node("Board").GetLayer(), 2);

        // The next outage starts with the short wait again
        _endpoint->Drop();
        Poll();
        const size_t connections = _endpoint->GetConnectionCount();
        PollAfter(Timing::kFirstRetry);
        EXPECT_EQ(_endpoint->GetConnectionCount(), connections + 1);
    }

    TEST_F(RemoteSessionTest, ARefusedSendEndsTheConnection) {
        ASSERT_NO_FATAL_FAILURE(Attach());

        Connection().refusesSend = true;
        PollAfter(Timing::kTreeInterval);
        Poll();

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Lost);
        EXPECT_TRUE(Connection().destroyed);
    }

    TEST_F(RemoteSessionTest, AProgramWithoutTheSceneMethodsIsNotRetried) {
        Start(FakeControlEndpoint::Methods::Builtins);
        ASSERT_TRUE(_session->Attach(kAddress).has_value());

        Exchange();

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Lost);
        EXPECT_NE(String(_session->GetLossReason()).find("unknown_method"), String::npos);
        EXPECT_TRUE(Connection().destroyed);
        PollAfter(Timing::kMaxRetry * 10);
        EXPECT_EQ(_endpoint->GetConnectionCount(), 1u);
    }

    TEST_F(RemoteSessionTest, MessagesThatCannotBeReadDoNoHarm) {
        ASSERT_TRUE(_session->Attach(kAddress).has_value());
        Poll();
        for (const StringView text : {"", "{", "[1]", "null", R"({"id":"1","ok":true})", R"({"id":-1})", R"({"id":999,"ok":true,"result":{}})", R"({"event":"state_changed","data":{}})"}) {
            Connection().PushText(text);
        }
        Poll();
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Connecting);

        AnswerWith("status", R"("ok":true,"result":5)");
        AnswerWith("schema", R"("ok":true,"result":[])");
        AnswerWith("scene_tree", R"("ok":true,"result":{"nodes":[{"path":"A","components":["X","X"]}]})");
        Poll();
        ASSERT_EQ(_session->GetState(), RemoteSessionState::Attached);

        _session->Select("A");
        Poll();
        AnswerWith("batch", R"("ok":true,"result":{"results":"none"})");
        Poll();
        ASSERT_EQ(_session->GetComponents().size(), 2u);
        EXPECT_FALSE(_session->GetComponents()[0].IsLoaded());

        PollAfter(Timing::kComponentInterval);
        AnswerWith("batch", R"("ok":true,"result":{"results":[7,{"ok":true,"result":{"node":1}},{"ok":true}]})");
        Poll();
        EXPECT_FALSE(_session->GetComponents()[1].IsLoaded());

        // A tree the program could not give leaves the one that was read
        PollAfter(Timing::kTreeInterval);
        AnswerWith("scene_tree", R"("ok":false,"error":"broken")");
        Poll();
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_NE(_session->GetTree().Find("A"), nullptr);
    }

    TEST_F(RemoteSessionTest, AttachSaysWhyItCannotAndDetachForgetsEverything) {
        RemoteSession withoutSockets([]() -> UniquePtr<IRoomTransport> { return nullptr; });
        const auto none = withoutSockets.Attach(kAddress);
        ASSERT_FALSE(none.has_value());
        EXPECT_NE(none.error().find("no sockets"), String::npos);
        EXPECT_EQ(withoutSockets.GetState(), RemoteSessionState::Detached);

        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        _session->GetWrites().Edit({"Board"}, PropertyValue{int64_t{4}}, RemoteEditKind::Text);
        // An address the transport refuses leaves the link as it is
        const auto bad = _session->Attach("nonsense");
        ASSERT_FALSE(bad.has_value());
        EXPECT_EQ(bad.error(), "not a ws:// address");
        EXPECT_EQ(_session->GetState(), RemoteSessionState::Attached);
        EXPECT_EQ(_session->GetAddress(), kAddress);

        FakeControlConnection& connection = Connection();
        _session->Detach();
        const size_t connections = _endpoint->GetConnectionCount();
        PollAfter(Timing::kMaxRetry);

        EXPECT_EQ(_session->GetState(), RemoteSessionState::Detached);
        EXPECT_TRUE(connection.destroyed);
        EXPECT_EQ(_endpoint->GetConnectionCount(), connections);
        EXPECT_TRUE(_session->GetAddress().empty());
        EXPECT_TRUE(_session->GetTree().GetNodes().empty());
        EXPECT_EQ(_session->GetSchema().FindType("TransformComponent"), nullptr);
        EXPECT_FALSE(_session->GetSelection().has_value());
        EXPECT_TRUE(_session->GetComponents().empty());
        EXPECT_TRUE(_session->GetWrites().GetWrites().empty());
    }

    TEST_F(RemoteSessionTest, APollWithNothingNewAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(AttachAndSelect("Board"));
        for (int round = 0; round < 3; ++round) {
            ExchangeAfter(Timing::kTreeInterval);
        }
        // The program has answered the tree and the components with what it said before
        PollAfter(Timing::kTreeInterval);
        _endpoint->Serve();

        const size_t unread = Connection().inbox.size() - Connection().nextEvent;
        ASSERT_EQ(unread, 2u);
        ExpectNoAllocations([this] {
            Poll();
            Poll();  // nothing has come and nothing is due
        });
        EXPECT_EQ(Connection().nextEvent, Connection().inbox.size());
    }

#if BE_WITH_REMOTE_CONTROL

    TEST_F(RemoteSessionTest, TheEditorsTransportTakesASchemaLongerThanAMessageOfTheRooms) {
        constexpr size_t kLongerThanTheDefaults = 3 * 1024 * 1024;
        const String padding(kLongerThanTheDefaults, 'a');
        ControlService service;
        RegisterControlBuiltins(service);
        RegisterSceneControl(service, []() -> Scene* { return nullptr; });
        service.RegisterHandler("schema", [&padding](ControlCall& call) {
            call.Reply([&padding](JsonWriter& writer) {
                writer.BeginObject();
                writer.Member("padding", StringView(padding));
                writer.EndObject();
            });
        });
        WebSocketServer server(service);
        const auto port = server.Start(0);
        ASSERT_TRUE(port.has_value());
        RemoteSession session;
        ASSERT_TRUE(session.Attach(Format("ws://127.0.0.1:{}", *port)).has_value());

        const bool settled = WaitUntil([&] {
            session.Poll(std::chrono::steady_clock::now());
            service.Drain();
            return session.GetState() != RemoteSessionState::Connecting;
        });

        EXPECT_TRUE(settled);
        EXPECT_EQ(session.GetState(), RemoteSessionState::Attached) << String(session.GetLossReason()).c_str();
        session.Detach();
        server.Stop();
    }

#endif  // BE_WITH_REMOTE_CONTROL

}  // namespace BECore::Tests
