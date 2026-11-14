#include "FakeRoomTransport.h"
#include "ManualClock.h"

#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Common/Printers.h>
#include <RoomClient/RoomClient.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr StringView kUrl = "ws://127.0.0.1:9000";
        constexpr StringView kRoom = "ABCDE";
        constexpr StringView kToken = "00112233445566778899aabbccddeeff";

        eastl::string Lobby(int hostSeat = 0, RoomPhase phase = RoomPhase::Lobby) {
            LobbySnapshot lobby;
            lobby.room = kRoom;
            lobby.game = "cards";
            lobby.phase = phase;
            lobby.hostSeat = hostSeat;
            lobby.seats.push_back(LobbySeat{0, SeatKind::Human, "Ann", true});
            lobby.seats.push_back(LobbySeat{1, SeatKind::Human, "Bob", true});
            lobby.seats.push_back(LobbySeat{2, SeatKind::Empty, {}, false});
            lobby.seats.push_back(LobbySeat{3, SeatKind::Empty, {}, false});
            return MakeLobbyMessage(lobby);
        }

        Json OneAction() {
            return Json::object({{"type", "play"}, {"card", 4}});
        }

        eastl::string View(uint64_t version, int seat = 0, bool finished = false) {
            Json view = Json::object({{"hand", Json::array({1, 2, 3})}});
            return MakeViewMessage(kRoom, seat, version, finished, view, Json::array({OneAction()}));
        }

        /// A RoomClient over fake transports, with a manual clock and the events it has reported so far
        class RoomClientTest : public ::testing::Test {
        protected:
            RoomClientTest() {
                MakeClient({});
            }

            /// A new client over the fake transports; @p settings has the numbers a test wants to change (its factory is replaced)
            void MakeClient(RoomClientSettings settings) {
                settings.transportFactory = _transports.Factory();
                _client = std::make_unique<RoomClient>(std::move(settings));
            }

            /// Polls once and takes the events
            void Pump() {
                _client->Poll(_clock.Now());
                _client->DrainEvents(_events);
            }

            /// Moves the clock and polls
            void Advance(milliseconds step) {
                _clock.Advance(step);
                Pump();
            }

            /// The connection breaks while in the room
            void Lose(FakeLink& link, uint16_t code = kRoomTransportAbnormalClosure, StringView reason = "connection lost") {
                link.PushClosed(code, reason);
                Pump();
            }

            /// The kinds of the events since the last Forget(), then forgets them
            [[nodiscard]] eastl::vector<RoomClientEventKind> TakeKinds() {
                auto kinds = Kinds();
                _events.clear();
                return kinds;
            }

            void Receive(FakeLink& link, StringView text) {
                link.PushText(text);
                Pump();
            }

            /// Host a room, open, and be told the room: the client is InRoom on seat 0
            FakeLink& EnterAsHost() {
                EXPECT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
                FakeLink& link = _transports.Last();
                link.PushOpened();
                Pump();
                link.PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
                link.PushText(Lobby());
                Pump();
                return link;
            }

            /// The kinds of the events so far, in order
            [[nodiscard]] eastl::vector<RoomClientEventKind> Kinds() const {
                eastl::vector<RoomClientEventKind> kinds;
                for (const RoomClientEvent& event : _events) {
                    kinds.push_back(event.kind);
                }
                return kinds;
            }

            [[nodiscard]] std::expected<ClientMessage, ControlError> SentParsed(const FakeLink& link, size_t index) const {
                return link.ParseSent(index);
            }

            template <typename T>
            [[nodiscard]] const T* AsSent(const std::expected<ClientMessage, ControlError>& parsed) const {
                return parsed ? std::get_if<T>(&*parsed) : nullptr;
            }

            ManualClock _clock;
            FakeTransports _transports;
            std::unique_ptr<RoomClient> _client;
            eastl::vector<RoomClientEvent> _events;
        };

        using Kind = RoomClientEventKind;

    }  // namespace

    // ---- Host and Join ------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, HostSendsCreateRoomAfterOpenedAndNotBefore) {
        Json options = Json::object({{"rounds", 4}});
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann", options).has_value());
        ASSERT_EQ(_transports.Count(), 1u);
        FakeLink& link = _transports.Last();
        EXPECT_EQ(link.url, kUrl);
        EXPECT_EQ(_client->GetState(), RoomClientState::Connecting);

        Pump();
        EXPECT_TRUE(link.sent.empty());
        EXPECT_EQ(_client->GetState(), RoomClientState::Connecting);

        link.PushOpened();
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        ASSERT_EQ(link.sent.size(), 1u);
        const auto parsed = SentParsed(link, 0);
        const auto* create = AsSent<CreateRoomMessage>(parsed);
        ASSERT_NE(create, nullptr);
        EXPECT_EQ(create->game, "cards");
        EXPECT_EQ(create->name, "Ann");
        EXPECT_EQ(create->options, options);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Connected);
    }

    TEST_F(RoomClientTest, JoinSendsJoinRoomWithTheNormalisedCode) {
        ASSERT_TRUE(_client->Join(kUrl, "abcde", "Bob").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        Pump();
        ASSERT_EQ(link.sent.size(), 1u);
        const auto parsed = SentParsed(link, 0);
        const auto* join = AsSent<JoinRoomMessage>(parsed);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->code, kRoom);
        EXPECT_EQ(join->name, "Bob");
        EXPECT_FALSE(join->IsReconnect());
    }

    TEST_F(RoomClientTest, JoinWithACodeThatCannotExistMakesNoConnection) {
        const auto joined = _client->Join(kUrl, "AB0DE", "Bob");
        ASSERT_FALSE(joined.has_value());
        EXPECT_EQ(joined.error().code, RoomErrors::kUnknownRoom);
        EXPECT_EQ(_transports.Count(), 0u);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
    }

    TEST_F(RoomClientTest, JoinedLobbyAndViewFillTheStateAndGiveTheEventsInOrder) {
        FakeLink& link = EnterAsHost();
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(_client->GetRoom(), kRoom);
        EXPECT_EQ(_client->GetPlayer(), 1u);
        EXPECT_EQ(_client->GetSeat(), 0);
        EXPECT_TRUE(_client->IsHost());
        EXPECT_EQ(_client->GetLobby().seats.size(), 4u);
        EXPECT_EQ(_client->GetLobby().seats[1].name, "Bob");
        EXPECT_FALSE(_client->HasView());
        const RoomSession& session = _client->GetSession();
        EXPECT_TRUE(session.IsValid());
        EXPECT_EQ(session.url, kUrl);
        EXPECT_EQ(session.room, kRoom);
        EXPECT_EQ(session.token, kToken);

        Receive(link, View(7, 0));
        EXPECT_TRUE(_client->HasView());
        EXPECT_EQ(_client->GetViewVersion(), 7u);
        EXPECT_FALSE(_client->IsFinished());
        EXPECT_EQ(_client->GetView(), Json::object({{"hand", Json::array({1, 2, 3})}}));
        EXPECT_EQ(_client->GetActions(), Json::array({OneAction()}));

        const eastl::vector<RoomClientEventKind> expected = {Kind::Connected, Kind::Joined, Kind::LobbyChanged, Kind::ViewChanged};
        EXPECT_EQ(Kinds(), expected);
    }

    TEST_F(RoomClientTest, TheHostFlagFollowsTheLobby) {
        FakeLink& link = EnterAsHost();
        EXPECT_TRUE(_client->IsHost());
        Receive(link, Lobby(1));
        EXPECT_FALSE(_client->IsHost());
        Receive(link, Lobby(0));
        EXPECT_TRUE(_client->IsHost());
    }

    TEST_F(RoomClientTest, ACommandThatDoesNotFitTheStateIsWrongState) {
        // Nothing connected
        EXPECT_EQ(_client->ChooseSeat(1).error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Start().error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->SendAction(OneAction()).error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Leave().error().code, RoomClientErrors::kWrongState);

        // Opening
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        EXPECT_EQ(_client->Host(kUrl, "cards", "Ann").error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Join(kUrl, "ABCDE", "Ann").error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Start().error().code, RoomClientErrors::kWrongState);

        // Waiting for the answer to create_room
        _transports.Last().PushOpened();
        Pump();
        EXPECT_EQ(_client->Join(kUrl, "ABCDE", "Ann").error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->ChooseSeat(1).error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_transports.Count(), 1u);
        EXPECT_EQ(_transports.Last().sent.size(), 1u);
    }

    TEST_F(RoomClientTest, ASecondJoinWhileInARoomIsWrongState) {
        FakeLink& link = EnterAsHost();
        const size_t sentBefore = link.sent.size();
        EXPECT_EQ(_client->Join(kUrl, "ABCDE", "Ann").error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Host(kUrl, "cards", "Ann").error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(link.sent.size(), sentBefore);
        EXPECT_EQ(_transports.Count(), 1u);
    }

    // ---- In the room --------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ChooseSeatStartAndLeaveAreSent) {
        FakeLink& link = EnterAsHost();
        ASSERT_TRUE(_client->ChooseSeat(2).has_value());
        ASSERT_TRUE(_client->Start().has_value());
        ASSERT_TRUE(_client->Leave().has_value());
        ASSERT_EQ(link.sent.size(), 4u);
        const auto seat = SentParsed(link, 1);
        const auto* chooseSeat = AsSent<ChooseSeatMessage>(seat);
        ASSERT_NE(chooseSeat, nullptr);
        EXPECT_EQ(chooseSeat->seat, 2);
        const auto start = SentParsed(link, 2);
        EXPECT_NE(AsSent<StartMessage>(start), nullptr);
        const auto leave = SentParsed(link, 3);
        EXPECT_NE(AsSent<LeaveMessage>(leave), nullptr);
    }

    TEST_F(RoomClientTest, ChooseSeatTakesTheSeatAtOnceAndGoesBackOnAnError) {
        FakeLink& link = EnterAsHost();
        ASSERT_TRUE(_client->ChooseSeat(2).has_value());
        EXPECT_EQ(_client->GetSeat(), 2);
        Receive(link, MakeErrorMessage(RoomErrors::kSeatTaken, "taken", RoomMessageTypes::kChooseSeat));
        EXPECT_EQ(_client->GetSeat(), 0);
        EXPECT_EQ(_client->GetLastError().code, RoomErrors::kSeatTaken);

        // A view says where the seat really is
        ASSERT_TRUE(_client->ChooseSeat(3).has_value());
        Receive(link, View(1, 3));
        EXPECT_EQ(_client->GetSeat(), 3);
    }

    TEST_F(RoomClientTest, LeaveForgetsTheSessionAndLeftBringsTheConnectionBackToConnected) {
        FakeLink& link = EnterAsHost();
        ASSERT_TRUE(_client->Leave().has_value());
        EXPECT_FALSE(_client->GetSession().IsValid());
        Receive(link, MakeLeftMessage("left"));
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_TRUE(_client->GetRoom().empty());
        EXPECT_EQ(_client->GetSeat(), -1);
        EXPECT_TRUE(_client->GetLobby().seats.empty());
        EXPECT_EQ(_events.back().kind, Kind::Left);
        EXPECT_EQ(_events.back().code, "left");
        EXPECT_FALSE(link.closeCalled);
    }

    TEST_F(RoomClientTest, ALeftFromTheServerClearsTheRoomAndTheConnectionCanJoinAgain) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(3));
        Receive(link, MakeLeftMessage("room_finished"));
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_FALSE(_client->HasView());
        EXPECT_EQ(_client->GetViewVersion(), 0u);
        EXPECT_FALSE(_client->GetSession().IsValid());
        EXPECT_EQ(_events.back().kind, Kind::Left);
        EXPECT_EQ(_events.back().code, "room_finished");

        // Another address needs another connection: the open one is not silently used for it
        EXPECT_EQ(_client->Join("ws://127.0.0.1:9001", "ABCDE", "Ann").error().code, RoomClientErrors::kWrongState);
        // The same address uses the open connection
        const size_t sentBefore = link.sent.size();
        ASSERT_TRUE(_client->Join(kUrl, "FGHJK", "Ann").has_value());
        EXPECT_EQ(_transports.Count(), 1u);
        ASSERT_EQ(link.sent.size(), sentBefore + 1);
        const auto parsed = SentParsed(link, sentBefore);
        const auto* join = AsSent<JoinRoomMessage>(parsed);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->code, "FGHJK");
    }

    // ---- Actions ------------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, SendActionStampsTheVersionOfTheLastView) {
        FakeLink& link = EnterAsHost();
        // In the room but the game has not started
        EXPECT_EQ(_client->SendAction(OneAction()).error().code, RoomClientErrors::kWrongState);

        Receive(link, View(7));
        ASSERT_TRUE(_client->SendAction(OneAction()).has_value());
        Receive(link, View(8));
        ASSERT_TRUE(_client->SendAction(OneAction()).has_value());

        ASSERT_EQ(link.sent.size(), 3u);
        const auto first = SentParsed(link, 1);
        const auto* firstAction = AsSent<ActionMessage>(first);
        ASSERT_NE(firstAction, nullptr);
        EXPECT_EQ(firstAction->version, 7u);
        EXPECT_EQ(firstAction->action, OneAction());
        const auto second = SentParsed(link, 2);
        const auto* secondAction = AsSent<ActionMessage>(second);
        ASSERT_NE(secondAction, nullptr);
        EXPECT_EQ(secondAction->version, 8u);
    }

    TEST_F(RoomClientTest, AStaleActionComesAsAnErrorThenTheFreshViewAndIsNotSentAgain) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(7));
        ASSERT_TRUE(_client->SendAction(OneAction()).has_value());
        _events.clear();

        link.PushText(MakeErrorMessage(RoomErrors::kStaleAction, "the game moved on", RoomMessageTypes::kAction));
        link.PushText(View(9));
        Pump();

        const eastl::vector<RoomClientEventKind> expected = {Kind::Error, Kind::ViewChanged};
        EXPECT_EQ(Kinds(), expected);
        EXPECT_EQ(_events[0].code, RoomErrors::kStaleAction);
        EXPECT_EQ(_events[0].ref, RoomMessageTypes::kAction);
        EXPECT_EQ(_client->GetViewVersion(), 9u);
        EXPECT_EQ(link.sent.size(), 2u);
    }

    TEST_F(RoomClientTest, AFinishedViewIsReported) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(12, 0, true));
        EXPECT_TRUE(_client->IsFinished());
    }

    // ---- Errors and bad input -----------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, AnErrorCarriesItsRefAndIsRemembered) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        Receive(link, MakeErrorMessage(RoomErrors::kNotHost, "only the host starts", RoomMessageTypes::kStart));
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Error);
        EXPECT_EQ(_events[0].code, RoomErrors::kNotHost);
        EXPECT_EQ(_events[0].message, "only the host starts");
        EXPECT_EQ(_events[0].ref, RoomMessageTypes::kStart);
        EXPECT_EQ(_client->GetLastError().code, RoomErrors::kNotHost);
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }

    TEST_F(RoomClientTest, ARefusedCreateLeavesTheConnectionOpenForAnotherTry) {
        ASSERT_TRUE(_client->Host(kUrl, "nope", "Ann").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(MakeErrorMessage(RoomErrors::kUnknownGame, "no such game", RoomMessageTypes::kCreateRoom));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_EQ(_events.back().code, RoomErrors::kUnknownGame);

        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        EXPECT_EQ(_transports.Count(), 1u);
        EXPECT_EQ(link.sent.size(), 2u);
    }

    TEST_F(RoomClientTest, GarbageAndUnknownMessagesDoNotBreakTheClient) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(5));
        _events.clear();

        Receive(link, "this is not json");
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Error);
        EXPECT_EQ(_events[0].code, RoomClientErrors::kBadServerMessage);
        EXPECT_EQ(_client->GetLastError().code, RoomClientErrors::kBadServerMessage);

        Receive(link, R"({"v":1,"type":"view","room":"ABCDE","seat":"x"})");
        EXPECT_EQ(_events.back().code, RoomClientErrors::kBadServerMessage);
        Receive(link, R"({"v":2,"type":"pong"})");
        EXPECT_EQ(_events.back().code, RoomClientErrors::kBadServerMessage);

        const size_t errors = _events.size();
        Receive(link, R"({"v":1,"type":"emote","who":3})");
        Receive(link, MakePongMessage());
        EXPECT_EQ(_events.size(), errors);

        // The state is as it was
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(_client->GetViewVersion(), 5u);
        EXPECT_EQ(_client->GetRoom(), kRoom);
    }

    TEST_F(RoomClientTest, AMessageOfAnotherRoomIsDropped) {
        FakeLink& link = EnterAsHost();
        LobbySnapshot other;
        other.room = "FGHJK";
        other.game = "cards";
        other.hostSeat = 3;
        const size_t lobbySeats = _client->GetLobby().seats.size();
        Receive(link, MakeLobbyMessage(other));
        EXPECT_EQ(_client->GetLobby().room, kRoom);
        EXPECT_EQ(_client->GetLobby().seats.size(), lobbySeats);
        Receive(link, MakeViewMessage("FGHJK", 1, 4, false, Json::object(), Json::array()));
        EXPECT_FALSE(_client->HasView());
    }

    TEST_F(RoomClientTest, ALobbyOrViewBeforeJoinedIsDropped) {
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(Lobby());
        link.PushText(View(1));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_FALSE(_client->HasView());
        const eastl::vector<RoomClientEventKind> expected = {Kind::Connected};
        EXPECT_EQ(Kinds(), expected);
    }

    // ---- The connection -----------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ATransportThatCannotStartIsAnErrorOfTheCommand) {
        _transports.FailNextOpen("tls_unsupported: use the browser build");
        const auto hosted = _client->Host("wss://example.org", "cards", "Ann");
        ASSERT_FALSE(hosted.has_value());
        EXPECT_EQ(hosted.error().code, RoomClientErrors::kConnectFailed);
        EXPECT_EQ(hosted.error().message, "tls_unsupported: use the browser build");
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_EQ(_client->GetLastError().code, RoomClientErrors::kConnectFailed);
        // Nothing was started: the next try is a clean one
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        EXPECT_EQ(_client->GetState(), RoomClientState::Connecting);
    }

    TEST_F(RoomClientTest, NoTransportIsAnErrorOfTheCommand) {
        _transports.SetUnavailable(true);
        const auto hosted = _client->Host(kUrl, "cards", "Ann");
        ASSERT_FALSE(hosted.has_value());
        EXPECT_EQ(hosted.error().code, RoomClientErrors::kTransportUnavailable);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);

        // No factory at all: the default one, which this test build may not have
        RoomClient withDefault;
        const auto joined = withDefault.Join(kUrl, "ABCDE", "Bob");
        EXPECT_TRUE(joined.has_value() || joined.error().code == RoomClientErrors::kTransportUnavailable || joined.error().code == RoomClientErrors::kConnectFailed);
    }

    TEST_F(RoomClientTest, AConnectThatIsRefusedGivesAClosedEvent) {
        ASSERT_TRUE(_client->Join(kUrl, "ABCDE", "Bob").has_value());
        _transports.Last().PushClosed(kRoomTransportAbnormalClosure, "connection refused");
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        EXPECT_EQ(_events[0].closeCode, kRoomTransportAbnormalClosure);
        EXPECT_EQ(_events[0].message, "connection refused");
        EXPECT_TRUE(_transports.Last().destroyed);
    }

    TEST_F(RoomClientTest, AConnectionThatEndsInTheRoomWithoutReconnectForgetsTheRoomAndKeepsTheSession) {
        RoomClientSettings settings;
        settings.reconnect = false;
        MakeClient(settings);
        FakeLink& link = EnterAsHost();
        Receive(link, View(2));
        link.PushClosed(1006, "the server went away");
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(_client->GetRoom().empty());
        EXPECT_FALSE(_client->HasView());
        EXPECT_TRUE(_client->GetSession().IsValid());
        EXPECT_EQ(_client->GetSession().token, kToken);
        ASSERT_GE(_events.size(), 2u);
        EXPECT_EQ(_events[_events.size() - 2].kind, Kind::ConnectionLost);
        EXPECT_FALSE(_events[_events.size() - 2].willRetry);
        EXPECT_EQ(_events.back().kind, Kind::Closed);
        EXPECT_EQ(_events.back().closeCode, 1006);
        EXPECT_TRUE(link.destroyed);
        EXPECT_EQ(_client->ChooseSeat(1).error().code, RoomClientErrors::kWrongState);
    }

    TEST_F(RoomClientTest, TextsBeforeTheCloseAreDeliveredFirst) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        link.PushText(View(3));
        link.PushText(MakeLeftMessage("room_finished"));
        link.PushClosed(1000, "bye");
        Pump();
        const eastl::vector<RoomClientEventKind> expected = {Kind::ViewChanged, Kind::Left, Kind::Closed};
        EXPECT_EQ(Kinds(), expected);
    }

    TEST_F(RoomClientTest, DisconnectClosesInEveryState) {
        // Nothing to close
        _client->Disconnect();
        EXPECT_TRUE(_events.empty());
        _client->DrainEvents(_events);
        EXPECT_TRUE(_events.empty());

        // While connecting
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        _client->Disconnect();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(_transports.Last().closeCalled);
        _client->DrainEvents(_events);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        EXPECT_EQ(_events[0].closeCode, kRoomTransportNormalClosure);
        _events.clear();

        // In a room: the transport finishes on its own and is let go of when it says so
        FakeLink& link = EnterAsHost();
        _events.clear();
        _client->Disconnect();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(_client->GetRoom().empty());
        EXPECT_TRUE(link.closeCalled);
        EXPECT_EQ(link.closeCode, kRoomTransportNormalClosure);
        EXPECT_FALSE(link.destroyed);
        EXPECT_TRUE(_client->GetSession().IsValid());
        Pump();
        EXPECT_TRUE(link.destroyed);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);

        // A new connection needs no waiting
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        EXPECT_EQ(_client->GetState(), RoomClientState::Connecting);
    }

    TEST_F(RoomClientTest, ANewConnectionLetsGoOfAConnectionThatIsStillClosing) {
        FakeLink& link = EnterAsHost();
        link.closeAnswers = false;
        _client->Disconnect();
        ASSERT_TRUE(_client->Join(kUrl, "ABCDE", "Bob").has_value());
        EXPECT_TRUE(link.destroyed);
        EXPECT_EQ(_transports.Count(), 2u);
    }

    TEST_F(RoomClientTest, TheClientCanBeDestroyedInEveryState) {
        {
            RoomClient idle;
        }
        {
            FakeTransports transports;
            RoomClientSettings settings;
            settings.transportFactory = transports.Factory();
            auto client = std::make_unique<RoomClient>(settings);
            ASSERT_TRUE(client->Host(kUrl, "cards", "Ann").has_value());
            client.reset();
            EXPECT_TRUE(transports.Last().destroyed);
        }
        {
            FakeTransports transports;
            RoomClientSettings settings;
            settings.transportFactory = transports.Factory();
            auto client = std::make_unique<RoomClient>(settings);
            ASSERT_TRUE(client->Host(kUrl, "cards", "Ann").has_value());
            transports.Last().PushOpened();
            transports.Last().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
            client->Poll(RoomTime{});
            EXPECT_EQ(client->GetState(), RoomClientState::InRoom);
            client.reset();
            EXPECT_TRUE(transports.Last().destroyed);
        }
        {
            FakeTransports transports;
            RoomClientSettings settings;
            settings.transportFactory = transports.Factory();
            auto client = std::make_unique<RoomClient>(settings);
            ASSERT_TRUE(client->Host(kUrl, "cards", "Ann").has_value());
            transports.Last().closeAnswers = false;
            client->Disconnect();
            client.reset();
            EXPECT_TRUE(transports.Last().destroyed);
        }
    }

    // ---- Events -------------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ALobbyOrViewChangeAnnouncedAndNotTakenIsAnnouncedOnce) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        link.PushText(Lobby(0));
        link.PushText(Lobby(1));
        link.PushText(View(1));
        link.PushText(View(2));
        link.PushText(Lobby(0));
        Pump();
        const eastl::vector<RoomClientEventKind> expected = {Kind::LobbyChanged, Kind::ViewChanged, Kind::LobbyChanged};
        EXPECT_EQ(Kinds(), expected);
        EXPECT_EQ(_client->GetViewVersion(), 2u);
        EXPECT_EQ(_client->GetLobby().hostSeat, 0);
    }

    TEST_F(RoomClientTest, TheQueueOfEventsIsBounded) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        for (size_t i = 0; i < kMaxQueuedRoomClientEvents + 10; ++i) {
            link.PushText(MakeErrorMessage(Format("e{}", i), "x"));
        }
        Pump();
        ASSERT_EQ(_events.size(), kMaxQueuedRoomClientEvents);
        // The oldest were dropped, the newest are there
        EXPECT_EQ(_events.back().code, Format("e{}", kMaxQueuedRoomClientEvents + 9));
        EXPECT_EQ(_events.front().code, "e10");
    }

    TEST_F(RoomClientTest, PollingWithNothingToDoLeavesTheBuffersAlone) {
        EnterAsHost();
        _events.clear();
        _events.reserve(16);
        const size_t capacity = _events.capacity();
        for (int i = 0; i < 100; ++i) {
            Pump();
        }
        EXPECT_TRUE(_events.empty());
        EXPECT_EQ(_events.capacity(), capacity);
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }

    TEST_F(RoomClientTest, DrainEventsAppendsAndEmptiesTheQueue) {
        FakeLink& link = EnterAsHost();
        // EnterAsHost drained already; the events are in _events
        const size_t taken = _events.size();
        link.PushText(MakePongMessage());
        link.PushText(View(1));
        Pump();
        EXPECT_EQ(_events.size(), taken + 1);
        eastl::vector<RoomClientEvent> again;
        _client->DrainEvents(again);
        EXPECT_TRUE(again.empty());
    }

    // ---- Heartbeat ----------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, APingGoesOutAfterTenSecondsOfSilenceAndNotBefore) {
        FakeLink& link = EnterAsHost();
        const size_t sent = link.sent.size();
        Advance(milliseconds(9999));
        EXPECT_EQ(link.sent.size(), sent);
        Advance(milliseconds(1));
        ASSERT_EQ(link.sent.size(), sent + 1);
        const auto parsed = SentParsed(link, sent);
        EXPECT_NE(AsSent<PingMessage>(parsed), nullptr);
    }

    TEST_F(RoomClientTest, OnlyOnePingIsOutstandingUntilSomethingArrives) {
        FakeLink& link = EnterAsHost();
        const size_t sent = link.sent.size();
        Advance(seconds(10));
        Advance(seconds(20));
        EXPECT_EQ(link.sent.size(), sent + 1);
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);

        // The answer is life: the next ping is ten seconds after it
        Receive(link, MakePongMessage());
        Advance(milliseconds(9999));
        EXPECT_EQ(link.sent.size(), sent + 1);
        Advance(milliseconds(1));
        EXPECT_EQ(link.sent.size(), sent + 2);
    }

    TEST_F(RoomClientTest, NoPingWhileMessagesKeepArriving) {
        FakeLink& link = EnterAsHost();
        const size_t sent = link.sent.size();
        for (int i = 0; i < 40; ++i) {
            Advance(seconds(5));
            Receive(link, i % 2 == 0 ? eastl::string(MakePongMessage()) : View(static_cast<uint64_t>(i) + 1));
        }
        EXPECT_EQ(link.sent.size(), sent);
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }

    TEST_F(RoomClientTest, AnyTextIsLifeEvenOneThatDoesNotParse) {
        FakeLink& link = EnterAsHost();
        Advance(seconds(30));
        Receive(link, "this is not json");
        Advance(seconds(30));
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }

    TEST_F(RoomClientTest, ThirtyFiveSecondsOfNothingLosesTheConnection) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(4));
        _events.clear();
        Advance(milliseconds(34999));
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        Advance(milliseconds(1));
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        EXPECT_TRUE(link.aborted);
        EXPECT_FALSE(link.closeCalled);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::ConnectionLost);
        EXPECT_TRUE(_events[0].willRetry);
        // The abandoned connection is let go of when it says it is over
        EXPECT_FALSE(link.destroyed);
        Pump();
        EXPECT_TRUE(link.destroyed);
    }

    // ---- Reconnect ----------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ALostConnectionIsRetriedAfterTheBackoffThatGrowsToItsLimit) {
        FakeLink& first = EnterAsHost();
        Receive(first, View(4));
        _events.clear();
        Lose(first);
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::ConnectionLost);
        EXPECT_TRUE(_events[0].willRetry);
        EXPECT_EQ(_events[0].closeCode, kRoomTransportAbnormalClosure);

        const milliseconds waits[] = {milliseconds(500), seconds(1), seconds(2), seconds(4), seconds(4), seconds(4)};
        for (const milliseconds wait : waits) {
            const size_t count = _transports.Count();
            Advance(wait - milliseconds(1));
            ASSERT_EQ(_transports.Count(), count) << "too early after " << wait.count() << " ms";
            Advance(milliseconds(1));
            ASSERT_EQ(_transports.Count(), count + 1) << "no attempt after " << wait.count() << " ms";
            EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
            Lose(_transports.Last(), kRoomTransportAbnormalClosure, "connection refused");
        }
        // A failed attempt is not announced again: the player was told once
        EXPECT_EQ(_events.size(), 1u);
    }

    TEST_F(RoomClientTest, TheRoomStaysReadableWhileReconnecting) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(4));
        Lose(link);
        EXPECT_EQ(_client->GetRoom(), kRoom);
        EXPECT_TRUE(_client->HasView());
        EXPECT_EQ(_client->GetViewVersion(), 4u);
        EXPECT_EQ(_client->GetLobby().seats.size(), 4u);
        EXPECT_FALSE(_client->IsInRoom());
        // But nothing can be sent into a room that is not there
        EXPECT_EQ(_client->SendAction(OneAction()).error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->ChooseSeat(1).error().code, RoomClientErrors::kWrongState);
        EXPECT_EQ(_client->Start().error().code, RoomClientErrors::kWrongState);
        EXPECT_TRUE(_client->GetSession().IsValid());
    }

    TEST_F(RoomClientTest, TheNewConnectionJoinsWithTheTokenAndNotTheNameAndIsResumed) {
        FakeLink& first = EnterAsHost();
        Receive(first, View(4));
        Lose(first);
        _events.clear();
        Advance(milliseconds(500));
        ASSERT_EQ(_transports.Count(), 2u);
        FakeLink& second = _transports.Last();
        EXPECT_EQ(second.url, kUrl);
        EXPECT_TRUE(second.sent.empty());

        second.PushOpened();
        Pump();
        ASSERT_EQ(second.sent.size(), 1u);
        const auto parsed = SentParsed(second, 0);
        const auto* join = AsSent<JoinRoomMessage>(parsed);
        ASSERT_NE(join, nullptr);
        EXPECT_TRUE(join->IsReconnect());
        EXPECT_EQ(join->token, kToken);
        EXPECT_EQ(join->code, kRoom);
        EXPECT_TRUE(join->name.empty());
        // Not a new connection to the player: no Connected event, still Reconnecting until the server says so
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        EXPECT_TRUE(_events.empty());

        second.PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        second.PushText(Lobby(0, RoomPhase::Playing));
        second.PushText(View(9, 0));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(_client->GetViewVersion(), 9u);
        EXPECT_EQ(_client->GetLobby().phase, RoomPhase::Playing);
        EXPECT_EQ(_client->GetSession().token, kToken);
        const eastl::vector<RoomClientEventKind> expected = {Kind::Resumed, Kind::LobbyChanged, Kind::ViewChanged};
        EXPECT_EQ(Kinds(), expected);
        EXPECT_TRUE(first.destroyed);

        // A second loss starts from the first backoff again
        _events.clear();
        Lose(second);
        const size_t count = _transports.Count();
        Advance(milliseconds(499));
        EXPECT_EQ(_transports.Count(), count);
        Advance(milliseconds(1));
        EXPECT_EQ(_transports.Count(), count + 1);
    }

    TEST_F(RoomClientTest, ACloseWithAnyOtherCodeIsRetried) {
        for (const uint16_t code : {uint16_t(1000), uint16_t(1001), uint16_t(1011), uint16_t(1013)}) {
            MakeClient({});
            _events.clear();
            FakeLink& link = EnterAsHost();
            Lose(link, code, "closed");
            EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting) << "code " << code;
            const size_t count = _transports.Count();
            Advance(milliseconds(500));
            EXPECT_EQ(_transports.Count(), count + 1) << "code " << code;
            // The attempt itself is closed with a try-again as well
            Lose(_transports.Last(), 1013, "the server is full");
            Advance(seconds(1));
            EXPECT_EQ(_transports.Count(), count + 2) << "code " << code;
        }
    }

    TEST_F(RoomClientTest, ATransportThatNeverOpensIsLostAfterTheConnectTimeout) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(milliseconds(500));
        ASSERT_EQ(_transports.Count(), 2u);
        FakeLink& attempt = _transports.Last();
        Advance(milliseconds(14999));
        EXPECT_FALSE(attempt.aborted);
        Advance(milliseconds(1));
        EXPECT_TRUE(attempt.aborted);
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        // A failed attempt: the next one is one backoff step later
        const size_t count = _transports.Count();
        Advance(milliseconds(999));
        EXPECT_EQ(_transports.Count(), count);
        Advance(milliseconds(1));
        EXPECT_EQ(_transports.Count(), count + 1);
    }

    TEST_F(RoomClientTest, AFirstConnectThatNeverOpensEndsWithAClosedEvent) {
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        FakeLink& link = _transports.Last();
        Pump();
        Advance(milliseconds(14999));
        EXPECT_EQ(_client->GetState(), RoomClientState::Connecting);
        Advance(milliseconds(1));
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(link.aborted);
        ASSERT_FALSE(_events.empty());
        EXPECT_EQ(_events.back().kind, Kind::Closed);
        EXPECT_EQ(_events.back().code, RoomClientCloseReasons::kConnectTimeout);
        // Without a room there is nothing to bring back
        Advance(seconds(60));
        EXPECT_EQ(_transports.Count(), 1u);
    }

    TEST_F(RoomClientTest, AnAttemptThatIsNeverAnsweredIsLostByTheSilenceRule) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(milliseconds(500));
        FakeLink& attempt = _transports.Last();
        attempt.PushOpened();
        Pump();
        ASSERT_EQ(attempt.sent.size(), 1u);
        Advance(milliseconds(34999));
        EXPECT_FALSE(attempt.aborted);
        EXPECT_EQ(attempt.sent.size(), 2u) << "a ping went out in the meantime";
        Advance(milliseconds(1));
        EXPECT_TRUE(attempt.aborted);
        const size_t count = _transports.Count();
        Advance(seconds(1));
        EXPECT_EQ(_transports.Count(), count + 1);
    }

    // ---- Where it stops -----------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ATokenThatTheServerRefusesEndsItForGood) {
        for (const StringView code : {RoomErrors::kBadToken, RoomErrors::kUnknownRoom}) {
            MakeClient({});
            _events.clear();
            FakeLink& first = EnterAsHost();
            Lose(first);
            Advance(milliseconds(500));
            FakeLink& attempt = _transports.Last();
            attempt.PushOpened();
            attempt.PushText(MakeErrorMessage(code, "no", RoomMessageTypes::kJoinRoom));
            _events.clear();
            Pump();
            EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected) << code.data();
            ASSERT_EQ(_events.size(), 1u);
            EXPECT_EQ(_events[0].kind, Kind::Closed);
            EXPECT_EQ(_events[0].code, RoomClientCloseReasons::kRoomGone);
            EXPECT_FALSE(_client->GetSession().IsValid());
            EXPECT_TRUE(_client->GetRoom().empty());
            EXPECT_TRUE(attempt.closeCalled);
            const size_t count = _transports.Count();
            Advance(std::chrono::minutes(1));
            EXPECT_EQ(_transports.Count(), count);
        }
    }

    TEST_F(RoomClientTest, AnotherAnswerToTheJoinIsFinalToo) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(milliseconds(500));
        FakeLink& attempt = _transports.Last();
        attempt.PushOpened();
        attempt.PushText(MakeErrorMessage(RoomErrors::kRateLimited, "slow down", RoomMessageTypes::kJoinRoom));
        _events.clear();
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].code, RoomErrors::kRateLimited);
        // The token was not refused, so it is kept
        EXPECT_TRUE(_client->GetSession().IsValid());
    }

    TEST_F(RoomClientTest, TakenOverByAnotherDeviceEndsItAndDoesNotFightForTheSeat) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        link.PushText(MakeLeftMessage("replaced"));
        link.PushClosed(1000, "replaced");
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        const eastl::vector<RoomClientEventKind> expected = {Kind::Left, Kind::Closed};
        EXPECT_EQ(Kinds(), expected);
        EXPECT_EQ(_events[0].code, "replaced");
        EXPECT_EQ(_events[1].code, RoomClientCloseReasons::kReplaced);
        EXPECT_FALSE(_client->GetSession().IsValid());
        Advance(std::chrono::minutes(1));
        EXPECT_EQ(_transports.Count(), 1u);
    }

    TEST_F(RoomClientTest, ACloseForBreakingTheLimitsIsNotRetried) {
        for (const StringView reason : {"rate_limited", "too_many_failed_joins"}) {
            MakeClient({});
            FakeLink& link = EnterAsHost();
            _events.clear();
            Lose(link, 1008, reason);
            EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
            ASSERT_EQ(_events.size(), 2u);
            EXPECT_EQ(_events[0].kind, Kind::ConnectionLost);
            EXPECT_FALSE(_events[0].willRetry);
            EXPECT_EQ(_events[1].kind, Kind::Closed);
            EXPECT_EQ(_events[1].code, reason);
            EXPECT_EQ(_events[1].closeCode, 1008);
            const size_t count = _transports.Count();
            Advance(std::chrono::minutes(1));
            EXPECT_EQ(_transports.Count(), count);
        }
    }

    TEST_F(RoomClientTest, ABreakOfTheLimitsOnAnAttemptStopsTheAttempts) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(milliseconds(500));
        _events.clear();
        Lose(_transports.Last(), 1008, "too_many_failed_joins");
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        EXPECT_EQ(_events[0].code, "too_many_failed_joins");
        const size_t count = _transports.Count();
        Advance(std::chrono::minutes(1));
        EXPECT_EQ(_transports.Count(), count);
    }

    TEST_F(RoomClientTest, TheClientGivesUpWhenTheConnectionDoesNotComeBackInTime) {
        RoomClientSettings settings;
        settings.reconnectGiveUp = seconds(5);
        MakeClient(settings);
        FakeLink& first = EnterAsHost();
        Lose(first);
        // Attempts at 0.5 s, 1.5 s and 3.5 s all fail; the next would be at 7.5 s
        for (const milliseconds step : {milliseconds(500), milliseconds(1000), milliseconds(2000)}) {
            Advance(step);
            Lose(_transports.Last(), kRoomTransportAbnormalClosure, "connection refused");
        }
        ASSERT_EQ(_transports.Count(), 4u);
        _events.clear();
        Advance(milliseconds(1499));
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        Advance(milliseconds(1));
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        EXPECT_EQ(_events[0].code, RoomClientCloseReasons::kGaveUp);
        EXPECT_FALSE(_client->GetSession().IsValid());
        Advance(std::chrono::minutes(1));
        EXPECT_EQ(_transports.Count(), 4u);
    }

    TEST_F(RoomClientTest, TheGiveUpTimeAlsoCutsAnAttemptThatIsUnderWay) {
        RoomClientSettings settings;
        settings.reconnectGiveUp = seconds(5);
        MakeClient(settings);
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(seconds(4));
        FakeLink& last = _transports.Last();
        Advance(seconds(1));
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(last.closeCalled);
    }

    TEST_F(RoomClientTest, LeaveWhileWaitingToRetryIsADisconnectAndForgetsTheSession) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        _events.clear();
        ASSERT_TRUE(_client->Leave().has_value());
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_FALSE(_client->GetSession().IsValid());
        EXPECT_TRUE(_client->GetRoom().empty());
        _client->DrainEvents(_events);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        Advance(std::chrono::minutes(1));
        EXPECT_EQ(_transports.Count(), 1u);
    }

    TEST_F(RoomClientTest, DisconnectDuringAnAttemptClosesItAndStopsTheRetries) {
        FakeLink& first = EnterAsHost();
        Lose(first);
        Advance(milliseconds(500));
        FakeLink& attempt = _transports.Last();
        _events.clear();
        _client->Disconnect();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(attempt.closeCalled);
        _client->DrainEvents(_events);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        // Disconnect keeps the session: the game may Resume() later
        EXPECT_TRUE(_client->GetSession().IsValid());
        Advance(std::chrono::minutes(1));
        EXPECT_EQ(_transports.Count(), 2u);
    }

    // ---- Resume -------------------------------------------------------------------------------------------------------------------

    TEST_F(RoomClientTest, ASessionThatCannotBeOneIsRefusedWithoutAConnection) {
        RoomSession session{eastl::string(kUrl), eastl::string(kRoom), eastl::string(kToken)};
        RoomSession noRoom = session;
        noRoom.room.clear();
        RoomSession noToken = session;
        noToken.token.clear();
        RoomSession badCode = session;
        badCode.room = "AB0DE";
        RoomSession badToken = session;
        badToken.token = "NOT-A-TOKEN";
        RoomSession noUrl = session;
        noUrl.url.clear();
        for (const RoomSession& bad : {noRoom, noToken, badCode, badToken, noUrl}) {
            const auto resumed = _client->Resume(bad);
            ASSERT_FALSE(resumed.has_value());
            EXPECT_EQ(resumed.error().code, RoomClientErrors::kInvalidSession);
        }
        EXPECT_EQ(_transports.Count(), 0u);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
    }

    TEST_F(RoomClientTest, ResumeConnectsAndJoinsByTokenAndAnswersWithResumed) {
        RoomClient& fresh = *_client;
        const RoomSession session{eastl::string(kUrl), "abcde", eastl::string(kToken)};
        ASSERT_TRUE(fresh.Resume(session).has_value());
        EXPECT_EQ(fresh.GetState(), RoomClientState::Reconnecting);
        EXPECT_EQ(fresh.GetRoom(), kRoom);
        ASSERT_EQ(_transports.Count(), 1u);
        FakeLink& link = _transports.Last();
        EXPECT_EQ(link.url, kUrl);
        EXPECT_EQ(fresh.Resume(session).error().code, RoomClientErrors::kWrongState);

        link.PushOpened();
        Pump();
        ASSERT_EQ(link.sent.size(), 1u);
        const auto parsed = SentParsed(link, 0);
        const auto* join = AsSent<JoinRoomMessage>(parsed);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->code, kRoom);
        EXPECT_EQ(join->token, kToken);
        EXPECT_TRUE(join->name.empty());

        link.PushText(MakeJoinedMessage(kRoom, 2, kToken, 1, false));
        link.PushText(Lobby(0, RoomPhase::Playing));
        link.PushText(View(3, 1));
        Pump();
        EXPECT_EQ(fresh.GetState(), RoomClientState::InRoom);
        EXPECT_EQ(fresh.GetSeat(), 1);
        EXPECT_EQ(fresh.GetPlayer(), 2u);
        EXPECT_FALSE(fresh.IsHost());
        EXPECT_TRUE(fresh.HasView());
        const eastl::vector<RoomClientEventKind> expected = {Kind::Resumed, Kind::LobbyChanged, Kind::ViewChanged};
        EXPECT_EQ(Kinds(), expected);
        EXPECT_EQ(fresh.GetSession().url, kUrl);
    }

    TEST_F(RoomClientTest, ResumeWithAStaleSessionEndsWithRoomGone) {
        const RoomSession session{eastl::string(kUrl), eastl::string(kRoom), eastl::string(kToken)};
        ASSERT_TRUE(_client->Resume(session).has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(MakeErrorMessage(RoomErrors::kBadToken, "no such member", RoomMessageTypes::kJoinRoom));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Closed);
        EXPECT_EQ(_events[0].code, RoomClientCloseReasons::kRoomGone);
        EXPECT_FALSE(_client->GetSession().IsValid());
        EXPECT_TRUE(_client->GetRoom().empty());
        EXPECT_TRUE(link.closeCalled);
    }

    TEST_F(RoomClientTest, ResumeRetriesWhenTheServerIsNotThereYet) {
        const RoomSession session{eastl::string(kUrl), eastl::string(kRoom), eastl::string(kToken)};
        ASSERT_TRUE(_client->Resume(session).has_value());
        Pump();
        Lose(_transports.Last(), kRoomTransportAbnormalClosure, "connection refused");
        EXPECT_EQ(_client->GetState(), RoomClientState::Reconnecting);
        Advance(milliseconds(499));
        EXPECT_EQ(_transports.Count(), 1u);
        Advance(milliseconds(1));
        ASSERT_EQ(_transports.Count(), 2u);

        FakeLink& second = _transports.Last();
        second.PushOpened();
        second.PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        EXPECT_EQ(Kinds().back(), Kind::Resumed);
    }

    TEST_F(RoomClientTest, ResumeThatCannotStartIsAnErrorOfTheCommand) {
        _transports.FailNextOpen("tls_unsupported: use the browser build");
        const RoomSession session{"wss://example.org", eastl::string(kRoom), eastl::string(kToken)};
        const auto resumed = _client->Resume(session);
        ASSERT_FALSE(resumed.has_value());
        EXPECT_EQ(resumed.error().code, RoomClientErrors::kConnectFailed);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_FALSE(_client->GetSession().IsValid());
    }

    // ---- The list of public rooms -------------------------------------------------------------------------------------------------

    namespace {

        eastl::string RoomsAnswer(bool more = false) {
            RoomListMessage list;
            list.game = "cards";
            RoomListing first;
            first.room = kRoom;
            first.host = "Ann";
            first.taken = 1;
            first.seats = 4;
            list.rooms.push_back(first);
            RoomListing second;
            second.room = "FGHJK";
            second.host = "Bob";
            second.taken = 3;
            second.seats = 4;
            second.options = Json::object({{"rounds", 4}});
            list.rooms.push_back(second);
            list.more = more;
            return MakeRoomListMessage(list);
        }

    }  // namespace

    TEST_F(RoomClientTest, ListRoomsAsksAfterOpenedAndTheAnswerFillsTheListAndGivesTheEvent) {
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        FakeLink& link = _transports.Last();
        Pump();
        EXPECT_TRUE(link.sent.empty());
        link.PushOpened();
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        ASSERT_EQ(link.sent.size(), 1u);
        const auto parsed = SentParsed(link, 0);
        const auto* list = AsSent<ListRoomsMessage>(parsed);
        ASSERT_NE(list, nullptr);
        EXPECT_EQ(list->game, "cards");
        EXPECT_TRUE(_client->GetRoomList().rooms.empty());

        _events.clear();
        Receive(link, RoomsAnswer(true));
        EXPECT_EQ(Kinds(), (eastl::vector<RoomClientEventKind>{Kind::RoomList}));
        const RoomListMessage& rooms = _client->GetRoomList();
        EXPECT_EQ(rooms.game, "cards");
        ASSERT_EQ(rooms.rooms.size(), 2u);
        EXPECT_EQ(rooms.rooms[0].room, kRoom);
        EXPECT_EQ(rooms.rooms[0].host, "Ann");
        EXPECT_EQ(rooms.rooms[1].taken, 3);
        EXPECT_EQ(rooms.rooms[1].options, Json::object({{"rounds", 4}}));
        EXPECT_TRUE(rooms.more);
        // A list is no room and no join: the state stays, and a failed join is not waited for
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_FALSE(_client->GetSession().IsValid());
    }

    TEST_F(RoomClientTest, ASecondListOnTheOpenConnectionReplacesTheFirstAnswer) {
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(RoomsAnswer());
        Pump();
        EXPECT_EQ(_client->GetRoomList().rooms.size(), 2u);

        // A list awaits no join, so the same connection serves the next one at once
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        EXPECT_EQ(_transports.Count(), 1u);
        EXPECT_EQ(link.sent.size(), 2u);
        RoomListMessage empty;
        empty.game = "cards";
        Receive(link, MakeRoomListMessage(empty));
        EXPECT_TRUE(_client->GetRoomList().rooms.empty());
        EXPECT_FALSE(_client->GetRoomList().more);
        EXPECT_EQ(_events.back().kind, Kind::RoomList);

        // Another address is another connection: not served by this one
        EXPECT_EQ(_client->ListRooms("ws://127.0.0.1:9001", "cards").error().code, RoomClientErrors::kWrongState);
    }

    TEST_F(RoomClientTest, ARateLimitedListIsAnErrorAndKeepsTheConnectionAndTheLastAnswer) {
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(RoomsAnswer());
        Pump();
        _events.clear();

        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        Receive(link, MakeErrorMessage(RoomErrors::kRateLimited, "slow down", RoomMessageTypes::kListRooms));
        ASSERT_EQ(_events.size(), 1u);
        EXPECT_EQ(_events[0].kind, Kind::Error);
        EXPECT_EQ(_events[0].code, RoomErrors::kRateLimited);
        EXPECT_EQ(_events[0].ref, RoomMessageTypes::kListRooms);
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
        EXPECT_FALSE(link.closeCalled);
        EXPECT_EQ(_client->GetRoomList().rooms.size(), 2u);

        // The connection serves a join of a listed code (the same message as a code typed by hand)
        ASSERT_TRUE(_client->Join(kUrl, kRoom, "Cy").has_value());
        EXPECT_EQ(_transports.Count(), 1u);
        ASSERT_EQ(link.sent.size(), 3u);
        const auto parsed = SentParsed(link, 2);
        const auto* join = AsSent<JoinRoomMessage>(parsed);
        ASSERT_NE(join, nullptr);
        EXPECT_EQ(join->code, kRoom);
        EXPECT_EQ(join->name, "Cy");
        link.PushText(MakeJoinedMessage(kRoom, 2, kToken, 1, false));
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
        EXPECT_TRUE(_client->GetRoomList().rooms.empty());
    }

    TEST_F(RoomClientTest, ARefusedListDoesNotClearTheWaitForAJoinAnswer) {
        ASSERT_TRUE(_client->Join(kUrl, kRoom, "Cy").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        Pump();
        // A join is under way: the list is refused by the state, and a stray list error does not end the wait
        EXPECT_EQ(_client->ListRooms(kUrl, "cards").error().code, RoomClientErrors::kWrongState);
        Receive(link, MakeErrorMessage(RoomErrors::kRateLimited, "slow down", RoomMessageTypes::kListRooms));
        EXPECT_EQ(_client->Join(kUrl, kRoom, "Cy").error().code, RoomClientErrors::kWrongState);
    }

    TEST_F(RoomClientTest, ARoomsMessageInARoomIsDropped) {
        FakeLink& link = EnterAsHost();
        _events.clear();
        Receive(link, RoomsAnswer());
        EXPECT_TRUE(_events.empty());
        EXPECT_TRUE(_client->GetRoomList().rooms.empty());
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }

    TEST_F(RoomClientTest, AMalformedRoomsMessageIsABadServerMessageAndKeepsTheLastAnswer) {
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(RoomsAnswer());
        Pump();
        _events.clear();

        // taken above seats, and a code in lower case
        Receive(link, R"({"v":1,"type":"rooms","game":"cards","rooms":[{"room":"ABCDE","host":"Ann","taken":5,"seats":4,"started":false}],"more":false})");
        Receive(link, R"({"v":1,"type":"rooms","game":"cards","rooms":[{"room":"abcde","host":"Ann","taken":1,"seats":4,"started":false}],"more":false})");
        ASSERT_EQ(_events.size(), 2u);
        EXPECT_EQ(_events[0].kind, Kind::Error);
        EXPECT_EQ(_events[0].code, RoomClientErrors::kBadServerMessage);
        EXPECT_EQ(_events[1].code, RoomClientErrors::kBadServerMessage);
        EXPECT_EQ(_client->GetRoomList().rooms.size(), 2u);
        EXPECT_EQ(_client->GetState(), RoomClientState::Connected);
    }

    TEST_F(RoomClientTest, AConnectionLostWhileBrowsingEndsWithClosedAndForgetsTheList) {
        ASSERT_TRUE(_client->ListRooms(kUrl, "cards").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        link.PushText(RoomsAnswer());
        Pump();
        _events.clear();

        Lose(link);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        ASSERT_FALSE(_events.empty());
        EXPECT_EQ(_events.back().kind, Kind::Closed);
        EXPECT_TRUE(_client->GetRoomList().rooms.empty());
        // No reconnect: the client does not open another connection by itself
        Advance(seconds(10));
        EXPECT_EQ(_transports.Count(), 1u);
    }

    TEST_F(RoomClientTest, ListRoomsOnAClosedTransportThatCannotStartIsAnErrorOfTheCommand) {
        _transports.FailNextOpen("connection refused");
        const auto listed = _client->ListRooms(kUrl, "cards");
        ASSERT_FALSE(listed.has_value());
        EXPECT_EQ(listed.error().code, RoomClientErrors::kConnectFailed);
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
    }

    TEST_F(RoomClientTest, HostWithPublicSendsThePublicMember) {
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann", Json::object(), true).has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        Pump();
        ASSERT_EQ(link.sent.size(), 1u);
        EXPECT_NE(link.sent[0].find("\"public\":true"), eastl::string::npos);
        const auto parsed = SentParsed(link, 0);
        const auto* create = AsSent<CreateRoomMessage>(parsed);
        ASSERT_NE(create, nullptr);
        EXPECT_TRUE(create->isPublic);
    }

    TEST_F(RoomClientTest, HostIsPrivateByDefaultAndSendsNoPublicMember) {
        ASSERT_TRUE(_client->Host(kUrl, "cards", "Ann").has_value());
        FakeLink& link = _transports.Last();
        link.PushOpened();
        Pump();
        ASSERT_EQ(link.sent.size(), 1u);
        EXPECT_EQ(link.sent[0].find("public"), eastl::string::npos);
    }

    // ---- Allocations --------------------------------------------------------------------------------------------------------------

#if BE_PROFILER_ALLOC
    TEST_F(RoomClientTest, PollingWithNothingToDoAllocatesNothing) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(1));
        _events.clear();
        _events.reserve(16);
        // Warm up: whatever the first polls make once
        for (int i = 0; i < 10; ++i) {
            Pump();
        }
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        // Five seconds of frames: inside the ping interval, so no ping is built
        for (int i = 0; i < 5000; ++i) {
            Advance(milliseconds(1));
        }
        const uint64_t after = AllocTracker::GetThreadCounters().count;
        EXPECT_EQ(after, before);
        EXPECT_EQ(_client->GetState(), RoomClientState::InRoom);
    }
#endif

}  // namespace BECore::Tests
