#include "FakeRoomTransport.h"

#include <Common/Printers.h>
#include <RoomClient/RoomClient.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr eastl::string_view kUrl = "ws://127.0.0.1:9000";
        constexpr eastl::string_view kRoom = "ABCDE";
        constexpr eastl::string_view kToken = "00112233445566778899aabbccddeeff";

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
                RoomClientSettings settings;
                settings.transportFactory = _transports.Factory();
                _client = std::make_unique<RoomClient>(std::move(settings));
            }

            /// Polls once and takes the events
            void Pump() {
                _client->Poll(RoomTime{});
                _client->DrainEvents(_events);
            }

            void Receive(FakeLink& link, eastl::string_view text) {
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

    TEST_F(RoomClientTest, AConnectionThatEndsInTheRoomForgetsTheRoomAndKeepsTheSession) {
        FakeLink& link = EnterAsHost();
        Receive(link, View(2));
        link.PushClosed(1006, "the server went away");
        Pump();
        EXPECT_EQ(_client->GetState(), RoomClientState::Disconnected);
        EXPECT_TRUE(_client->GetRoom().empty());
        EXPECT_FALSE(_client->HasView());
        EXPECT_TRUE(_client->GetSession().IsValid());
        EXPECT_EQ(_client->GetSession().token, kToken);
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

}  // namespace BECore::Tests
