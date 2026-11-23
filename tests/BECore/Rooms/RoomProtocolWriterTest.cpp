#include <BECore/Memory/ScratchArena.h>
#include <BECore/Rooms/RoomMessageReference.h>
#include <BECore/Threading/Thread.h>
#include <Common/AllocCheck.h>
#include <Common/Printers.h>
#include <Rooms/RoomProtocol.h>
#include <gtest/gtest.h>
#include <limits>

namespace BECore::Tests {

    namespace {

        constexpr StringView kRoomCode = "ABCDE";
        constexpr StringView kToken = "0123456789abcdef0123456789abcdef";
        constexpr size_t kDeepLevels = 300;
        constexpr int kFullSeatCount = 8;

        /// Every escape of JSON, a quote, a backslash, control characters, non-ASCII text and bytes that are not UTF-8
        const Vector<String>& HardTexts() {
            static const Vector<String> texts = {
                String(),
                String("plain"),
                String("quote \" backslash \\ slash /"),
                String("\b\f\n\r\t"),
                String("nul \0 and \x01\x1f\x7f", 17),
                String("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"),
                String("bad \xC3( \xFF \xE2\x82 \xED\xA0\x80 end"),
            };
            return texts;
        }

        Json MakeDeepArray(size_t levels) {
            Json value = Json::array();
            for (size_t i = 1; i < levels; ++i) {
                Json outer = Json::array();
                outer.push_back(std::move(value));
                value = std::move(outer);
            }
            return value;
        }

        /// Invalid UTF-8 in a string does not read back as it was written (the writer replaces it), so a value that is read back leaves it out
        Json MakeHardJson(bool withInvalidText = true) {
            Json value = Json::object();
            value["nested"] = Json{{"a", Json::array({1, 2, Json::object()})}, {"b", Json::object()}};
            value["big"] = 1e300;
            value["unsigned"] = std::numeric_limits<uint64_t>::max();
            value["negative"] = std::numeric_limits<int64_t>::min();
            if (withInvalidText) {
                value["badText"] = MakeJsonString(String("bad \xC3( \xFF"));
            }
            value["null"] = nullptr;
            value["real"] = 0.1;
            return value;
        }

        LobbySnapshot MakeLobby(int seatCount) {
            LobbySnapshot lobby;
            lobby.room = kRoomCode;
            lobby.game = "cards";
            lobby.phase = RoomPhase::Playing;
            lobby.hostSeat = 0;
            for (int i = 0; i < seatCount; ++i) {
                LobbySeat seat;
                seat.seat = i;
                seat.kind = i % 3 == 0 ? SeatKind::Human : (i % 3 == 1 ? SeatKind::Computer : SeatKind::Empty);
                if (seat.kind == SeatKind::Human) {
                    seat.name = Format("Player {}", i);
                    seat.connected = i % 2 == 0;
                }
                lobby.seats.push_back(std::move(seat));
            }
            return lobby;
        }

        RoomListMessage MakeRoomList(size_t count) {
            RoomListMessage list;
            list.game = "cards";
            for (size_t i = 0; i < count; ++i) {
                RoomListing listing;
                listing.room = kRoomCode;
                listing.host = Format("Host {}", i);
                listing.taken = static_cast<int>(i % 3);
                listing.seats = 4;
                listing.started = i % 2 == 1;
                list.rooms.push_back(std::move(listing));
            }
            return list;
        }

        /// Both paths must give the same text; the literal tests pin what that text is
        void ExpectLobby(const LobbySnapshot& lobby) {
            EXPECT_EQ(MakeLobbyMessage(lobby), RoomMessageReference::MakeLobbyMessage(lobby));
        }

        void ExpectRoomList(const RoomListMessage& list) {
            EXPECT_EQ(MakeRoomListMessage(list), RoomMessageReference::MakeRoomListMessage(list));
        }

        void ExpectView(StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
            EXPECT_EQ(MakeViewMessage(room, seat, version, finished, view, actions), RoomMessageReference::MakeViewMessage(room, seat, version, finished, view, actions));
        }

        constexpr StringView kLongGame = "a game whose name is longer than the small string";
        constexpr StringView kLongToken = "0123456789abcdef0123456789abcdef";
        constexpr StringView kLongText = "a text of more than twenty-three bytes";
        constexpr int kLongLobbySeats = 8;
        constexpr size_t kLongListedRooms = 20;
        constexpr uint64_t kLongVersion = 42;

        /// Every writer with texts past the small-string buffer, so a copy of any of them would be a heap block
        struct LongMessages {
            LobbySnapshot lobby = MakeLobby(kLongLobbySeats);
            RoomListMessage list = MakeRoomList(kLongListedRooms);
            Json view = Json{{"hand", Json::array({1, 2, 3})}, {"table", MakeJsonString(String(kLongText))}};
            Json actions = Json::array({Json{{"play", 1}}, Json{{"pass", true}}});
            ClientMessage create = CreateRoomMessage{String(kLongGame), String(kLongText), Json{{"rounds", 3}}, true};
            ClientMessage join = JoinRoomMessage{String(kRoomCode), String(), String(kLongToken)};
            ClientMessage listRooms = ListRoomsMessage{String(kLongGame)};

            LongMessages() {
                lobby.game = kLongGame;
                lobby.options = Json{{"fast", true}, {"rounds", 3}};
                for (LobbySeat& seat : lobby.seats) {
                    if (seat.kind == SeatKind::Human) {
                        seat.name = kLongText;
                    }
                }
                for (RoomListing& listing : list.rooms) {
                    listing.host = kLongText;
                    listing.options = Json{{"fast", true}};
                }
            }

            /// @p out is cleared first, so the round of a warmed-up String needs no growth
            void WriteAll(String& out) const {
                out.clear();
                AppendViewMessage(out, kRoomCode, 1, kLongVersion, false, view, actions);
                AppendLobbyMessage(out, lobby);
                AppendRoomListMessage(out, list);
                AppendErrorMessage(out, "illegal_action", kLongText, "action");
                AppendJoinedMessage(out, kRoomCode, 7, kLongToken, 2, true);
                AppendLeftMessage(out, kLongText);
                AppendPongMessage(out);
                AppendClientMessage(out, create);
                AppendClientMessage(out, join);
                AppendClientMessage(out, listRooms);
            }
        };

    }  // namespace

    TEST(RoomProtocolWriterTest, ServerMessagesAreWrittenWithTheirKeysInAlphabeticalOrder) {
        EXPECT_EQ(MakeJoinedMessage(kRoomCode, 7, kToken, 2, false), R"({"host":false,"player":7,"room":"ABCDE","seat":2,"token":"0123456789abcdef0123456789abcdef","type":"joined","v":1})");

        LobbySnapshot lobby;
        lobby.room = kRoomCode;
        lobby.game = "cards";
        lobby.hostSeat = 0;
        lobby.seats = {LobbySeat{0, SeatKind::Human, "Ann", true}, LobbySeat{1, SeatKind::Computer, {}, false}, LobbySeat{2, SeatKind::Empty, {}, false}, LobbySeat{3, SeatKind::Human, "Bob", false}};
        EXPECT_EQ(MakeLobbyMessage(lobby), R"({"game":"cards","host":0,"phase":"lobby","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Ann","seat":0},)"
                                           R"({"kind":"computer","seat":1},{"kind":"empty","seat":2},{"connected":false,"kind":"human","name":"Bob","seat":3}],"type":"lobby","v":1})");

        const Json view = Json{{"hand", Json::array({1, 2})}, {"turn", 1}};
        const Json actions = Json::array({Json{{"type", "play"}}});
        EXPECT_EQ(MakeViewMessage(kRoomCode, 1, 5, false, view, actions),
                  R"({"actions":[{"type":"play"}],"finished":false,"room":"ABCDE","seat":1,"type":"view","v":1,"version":5,"view":{"hand":[1,2],"turn":1}})");

        RoomListMessage list = MakeRoomList(2);
        list.rooms[0].host = "Ann";
        list.rooms[0].taken = 1;
        list.rooms[1].host = "Bob";
        list.rooms[1].taken = 2;
        list.rooms[1].options = Json{{"fast", true}};
        EXPECT_EQ(MakeRoomListMessage(list), R"({"game":"cards","more":false,"rooms":[{"host":"Ann","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Bob","options":{"fast":true},"room":"ABCDE","seats":4,"started":true,"taken":2}],"type":"rooms","v":1})");

        EXPECT_EQ(MakeErrorMessage("bad_message", "no", "start"), R"({"code":"bad_message","message":"no","ref":"start","type":"error","v":1})");
        EXPECT_EQ(MakeErrorMessage("bad_message", "no"), R"({"code":"bad_message","message":"no","type":"error","v":1})");
        EXPECT_EQ(MakeLeftMessage("kicked"), R"({"reason":"kicked","type":"left","v":1})");
        EXPECT_EQ(MakePongMessage(), R"({"type":"pong","v":1})");
    }

    TEST(RoomProtocolWriterTest, ALobbyIsTheSameAsTheDomPathForEveryOptionsShapeAndSeatCount) {
        for (const int seatCount : {0, 1, kFullSeatCount}) {
            ExpectLobby(MakeLobby(seatCount));
        }
        LobbySnapshot lobby = MakeLobby(3);
        lobby.options = Json{{"fast", true}, {"rounds", 3}};
        ExpectLobby(lobby);
        EXPECT_NE(MakeLobbyMessage(lobby).find(R"("options":{"fast":true,"rounds":3})"), String::npos);

        // options that is not a non-empty object is left out, as the DOM path leaves it out
        for (const Json& options : {Json::object(), Json::array({1}), Json(5), Json("text"), Json(nullptr)}) {
            lobby.options = options;
            ExpectLobby(lobby);
            EXPECT_EQ(MakeLobbyMessage(lobby).find("options"), String::npos) << options.dump();
        }
        lobby.phase = RoomPhase::Finished;
        lobby.hostSeat = -1;
        ExpectLobby(lobby);
    }

    TEST(RoomProtocolWriterTest, TextsWithEscapesAndInvalidUtf8AreWrittenAsTheDomPathWritesThem) {
        for (const String& text : HardTexts()) {
            LobbySnapshot lobby = MakeLobby(kFullSeatCount);
            lobby.room = text;
            lobby.game = text;
            for (LobbySeat& seat : lobby.seats) {
                seat.name = text;
            }
            ExpectLobby(lobby);

            RoomListMessage list = MakeRoomList(2);
            list.game = text;
            list.rooms[0].room = text;
            list.rooms[1].host = text;
            ExpectRoomList(list);

            EXPECT_EQ(MakeJoinedMessage(text, 1, text, 0, true), RoomMessageReference::MakeJoinedMessage(text, 1, text, 0, true));
            EXPECT_EQ(MakeErrorMessage(text, text, text), RoomMessageReference::MakeErrorMessage(text, text, text));
            EXPECT_EQ(MakeLeftMessage(text), RoomMessageReference::MakeLeftMessage(text));
            ExpectView(text, 0, 1, false, Json(MakeJsonString(text)), Json::array({MakeJsonString(text)}));
        }
    }

    TEST(RoomProtocolWriterTest, ARoomListIsTheSameAsTheDomPath) {
        ExpectRoomList(MakeRoomList(0));
        RoomListMessage list = MakeRoomList(20);
        list.more = true;
        ExpectRoomList(list);
        list.rooms[0].options = Json{{"fast", true}};
        list.rooms[1].options = Json::object();
        list.rooms[2].options = Json::array({1});
        list.rooms[3].options = Json(7);
        ExpectRoomList(list);
        const String text = MakeRoomListMessage(list);
        EXPECT_NE(text.find(R"("options":{"fast":true})"), String::npos);
        EXPECT_EQ(text.find("options", text.find("options") + 1), String::npos);
    }

    TEST(RoomProtocolWriterTest, AViewCarriesTheGamesJsonAsItIs) {
        ExpectView(kRoomCode, 3, 9, true, MakeHardJson(), Json::array({MakeHardJson(), Json::object()}));
        ExpectView(kRoomCode, 0, 0, false, Json::object(), Json::array());
        // a view and actions of another shape than the contract says are written as they are
        ExpectView(kRoomCode, 0, 0, false, Json(nullptr), Json::object());
        ExpectView(kRoomCode, 0, 0, false, Json::array({1}), Json(5));
        // the cut of a container that is too deep is the same on both paths
        ExpectView(kRoomCode, 0, 0, false, MakeDeepArray(kDeepLevels), MakeDeepArray(kDeepLevels));
    }

    TEST(RoomProtocolWriterTest, TheNumbersOfAMessageKeepTheirLimits) {
        constexpr uint64_t kMaxVersion = std::numeric_limits<uint64_t>::max();
        constexpr PlayerId kMaxPlayer = std::numeric_limits<PlayerId>::max();
        constexpr int kMinSeat = std::numeric_limits<int>::min();
        constexpr int kMaxSeat = std::numeric_limits<int>::max();
        for (const uint64_t version : {uint64_t{0}, uint64_t{std::numeric_limits<int64_t>::max()}, kMaxVersion}) {
            ExpectView(kRoomCode, kMinSeat, version, false, Json::object(), Json::array());
            ExpectView(kRoomCode, kMaxSeat, version, true, Json::object(), Json::array());
        }
        EXPECT_EQ(MakeJoinedMessage(kRoomCode, kMaxPlayer, kToken, kMinSeat, true), RoomMessageReference::MakeJoinedMessage(kRoomCode, kMaxPlayer, kToken, kMinSeat, true));
        EXPECT_EQ(MakeJoinedMessage(kRoomCode, 0, kToken, kMaxSeat, false), RoomMessageReference::MakeJoinedMessage(kRoomCode, 0, kToken, kMaxSeat, false));
        EXPECT_NE(MakeViewMessage(kRoomCode, 0, kMaxVersion, false, Json::object(), Json::array()).find("18446744073709551615"), String::npos);
    }

    TEST(RoomProtocolWriterTest, AppendKeepsWhatTheStringHoldsAndTheMakeFunctionsReturnTheSameBytes) {
        const LobbySnapshot lobby = MakeLobby(3);
        const RoomListMessage list = MakeRoomList(2);
        const Json view = Json{{"hand", Json::array({1, 2})}};
        const Json actions = Json::array({1});

        const Vector<Pair<String, String>> cases = {
            {MakeJoinedMessage(kRoomCode, 7, kToken, 2, true),
             [&] {
                 String out;
                 AppendJoinedMessage(out, kRoomCode, 7, kToken, 2, true);
                 return out;
             }()},
            {MakeLobbyMessage(lobby),
             [&] {
                 String out;
                 AppendLobbyMessage(out, lobby);
                 return out;
             }()},
            {MakeViewMessage(kRoomCode, 1, 5, false, view, actions),
             [&] {
                 String out;
                 AppendViewMessage(out, kRoomCode, 1, 5, false, view, actions);
                 return out;
             }()},
            {MakeRoomListMessage(list),
             [&] {
                 String out;
                 AppendRoomListMessage(out, list);
                 return out;
             }()},
            {MakeErrorMessage("code", "text", "ref"),
             [&] {
                 String out;
                 AppendErrorMessage(out, "code", "text", "ref");
                 return out;
             }()},
            {MakeLeftMessage("why"),
             [&] {
                 String out;
                 AppendLeftMessage(out, "why");
                 return out;
             }()},
            {MakePongMessage(),
             [&] {
                 String out;
                 AppendPongMessage(out);
                 return out;
             }()},
        };
        for (const auto& [made, appended] : cases) {
            EXPECT_EQ(made, appended);
        }

        constexpr StringView kPrefix = "kept:";
        String out(kPrefix.data(), kPrefix.size());
        AppendPongMessage(out);
        AppendLeftMessage(out, "why");
        EXPECT_EQ(out, String("kept:") + MakePongMessage() + MakeLeftMessage("why"));
    }

    TEST(RoomProtocolWriterTest, ParseServerMessageReadsEveryWrittenMessageBackToItsValues) {
        const auto joined = ParseServerMessage(MakeJoinedMessage(kRoomCode, std::numeric_limits<PlayerId>::max(), kToken, 2, true));
        ASSERT_TRUE(joined.has_value());
        const auto* joinedMessage = std::get_if<JoinedMessage>(&*joined);
        ASSERT_NE(joinedMessage, nullptr);
        EXPECT_EQ(joinedMessage->room, kRoomCode);
        EXPECT_EQ(joinedMessage->player, std::numeric_limits<PlayerId>::max());
        EXPECT_EQ(joinedMessage->token, kToken);
        EXPECT_EQ(joinedMessage->seat, 2);
        EXPECT_TRUE(joinedMessage->host);

        LobbySnapshot lobby = MakeLobby(kFullSeatCount);
        lobby.options = Json{{"fast", true}, {"rounds", 3}};
        const auto lobbyParsed = ParseServerMessage(MakeLobbyMessage(lobby));
        ASSERT_TRUE(lobbyParsed.has_value());
        const auto* lobbyMessage = std::get_if<LobbyMessage>(&*lobbyParsed);
        ASSERT_NE(lobbyMessage, nullptr);
        EXPECT_EQ(lobbyMessage->room, lobby.room);
        EXPECT_EQ(lobbyMessage->phase, lobby.phase);
        EXPECT_EQ(lobbyMessage->hostSeat, lobby.hostSeat);
        EXPECT_EQ(lobbyMessage->options, lobby.options);
        ASSERT_EQ(lobbyMessage->seats.size(), lobby.seats.size());
        for (size_t i = 0; i < lobby.seats.size(); ++i) {
            EXPECT_EQ(lobbyMessage->seats[i].seat, lobby.seats[i].seat);
            EXPECT_EQ(lobbyMessage->seats[i].kind, lobby.seats[i].kind);
            EXPECT_EQ(lobbyMessage->seats[i].name, lobby.seats[i].name);
            EXPECT_EQ(lobbyMessage->seats[i].connected, lobby.seats[i].connected);
        }

        // the reader takes a version up to INT64_MAX (it is an int64 there); the writer writes any uint64_t
        constexpr uint64_t kMaxReadableVersion = std::numeric_limits<int64_t>::max();
        const Json view = Json{{"hand", Json::array({1, 2})}, {"big", 1e300}, {"unsigned", std::numeric_limits<uint64_t>::max()}};
        const Json actions = Json::array({Json{{"type", "play"}}});
        const auto viewParsed = ParseServerMessage(MakeViewMessage(kRoomCode, 4, kMaxReadableVersion, true, view, actions));
        ASSERT_TRUE(viewParsed.has_value());
        const auto* viewMessage = std::get_if<ViewMessage>(&*viewParsed);
        ASSERT_NE(viewMessage, nullptr);
        EXPECT_EQ(viewMessage->seat, 4);
        EXPECT_EQ(viewMessage->version, kMaxReadableVersion);
        EXPECT_TRUE(viewMessage->finished);
        EXPECT_EQ(viewMessage->view, view);
        EXPECT_EQ(viewMessage->actions, actions);

        RoomListMessage list = MakeRoomList(3);
        list.more = true;
        list.rooms[1].options = Json{{"fast", true}};
        const auto listParsed = ParseServerMessage(MakeRoomListMessage(list));
        ASSERT_TRUE(listParsed.has_value());
        const auto* listMessage = std::get_if<RoomListMessage>(&*listParsed);
        ASSERT_NE(listMessage, nullptr);
        EXPECT_TRUE(listMessage->more);
        ASSERT_EQ(listMessage->rooms.size(), list.rooms.size());
        for (size_t i = 0; i < list.rooms.size(); ++i) {
            EXPECT_EQ(listMessage->rooms[i].host, list.rooms[i].host);
            EXPECT_EQ(listMessage->rooms[i].taken, list.rooms[i].taken);
            EXPECT_EQ(listMessage->rooms[i].started, list.rooms[i].started);
            EXPECT_EQ(listMessage->rooms[i].options, list.rooms[i].options);
        }

        const auto error = ParseServerMessage(MakeErrorMessage("bad_message", "no", "start"));
        ASSERT_TRUE(error.has_value());
        const auto* errorMessage = std::get_if<ErrorMessage>(&*error);
        ASSERT_NE(errorMessage, nullptr);
        EXPECT_EQ(errorMessage->code, "bad_message");
        EXPECT_EQ(errorMessage->message, "no");
        EXPECT_EQ(errorMessage->ref, "start");

        const auto left = ParseServerMessage(MakeLeftMessage("kicked"));
        ASSERT_TRUE(left.has_value());
        ASSERT_NE(std::get_if<LeftMessage>(&*left), nullptr);
        EXPECT_EQ(std::get_if<LeftMessage>(&*left)->reason, "kicked");

        const auto pong = ParseServerMessage(MakePongMessage());
        ASSERT_TRUE(pong.has_value());
        EXPECT_NE(std::get_if<PongMessage>(&*pong), nullptr);
    }

    namespace {

        /// @p actionVersion: the writer writes any uint64_t, the reader takes up to INT64_MAX
        Vector<ClientMessage> MakeClientMessages(uint64_t actionVersion) {
            CreateRoomMessage create;
            create.game = "cards";
            create.name = "Ann";
            ClientMessage createPublic = [&] {
                CreateRoomMessage message = create;
                message.isPublic = true;
                message.options = MakeHardJson(false);
                return message;
            }();
            JoinRoomMessage joinNew;
            joinNew.code = kRoomCode;
            joinNew.name = "Bob";
            JoinRoomMessage joinBack;
            joinBack.code = kRoomCode;
            joinBack.token = kToken;
            ActionMessage action;
            action.version = actionVersion;
            action.action = MakeHardJson(false);
            ActionMessage smallAction;
            smallAction.version = 3;
            smallAction.action = Json{{"card", 4}};
            ListRoomsMessage list;
            list.game = "cards";
            return {create,      createPublic,   joinNew,       joinBack, ChooseSeatMessage{std::numeric_limits<int64_t>::min()}, ChooseSeatMessage{2}, StartMessage{}, action,
                    smallAction, LeaveMessage{}, PingMessage{}, list};
        }

    }  // namespace

    TEST(RoomProtocolWriterTest, ClientMessagesAreWrittenWithTheirKeysInAlphabeticalOrder) {
        CreateRoomMessage create;
        create.game = "cards";
        create.name = "Ann";
        EXPECT_EQ(SerializeClientMessage(create), R"({"game":"cards","name":"Ann","options":{},"type":"create_room","v":1})");
        create.isPublic = true;
        create.options = Json{{"fast", true}};
        EXPECT_EQ(SerializeClientMessage(create), R"({"game":"cards","name":"Ann","options":{"fast":true},"public":true,"type":"create_room","v":1})");

        JoinRoomMessage join;
        join.code = kRoomCode;
        join.name = "Bob";
        EXPECT_EQ(SerializeClientMessage(join), R"({"code":"ABCDE","name":"Bob","type":"join_room","v":1})");
        join.name.clear();
        join.token = kToken;
        EXPECT_EQ(SerializeClientMessage(join), R"({"code":"ABCDE","token":"0123456789abcdef0123456789abcdef","type":"join_room","v":1})");

        EXPECT_EQ(SerializeClientMessage(ChooseSeatMessage{2}), R"({"seat":2,"type":"choose_seat","v":1})");
        EXPECT_EQ(SerializeClientMessage(StartMessage{}), R"({"type":"start","v":1})");
        ActionMessage action;
        action.version = 7;
        action.action = Json{{"card", 4}};
        EXPECT_EQ(SerializeClientMessage(action), R"({"action":{"card":4},"type":"action","v":1,"version":7})");
        EXPECT_EQ(SerializeClientMessage(LeaveMessage{}), R"({"type":"leave","v":1})");
        EXPECT_EQ(SerializeClientMessage(PingMessage{}), R"({"type":"ping","v":1})");
        ListRoomsMessage list;
        list.game = "cards";
        EXPECT_EQ(SerializeClientMessage(list), R"({"game":"cards","type":"list_rooms","v":1})");
    }

    TEST(RoomProtocolWriterTest, ClientMessagesAreTheSameAsTheDomPath) {
        for (const ClientMessage& message : MakeClientMessages(std::numeric_limits<uint64_t>::max())) {
            EXPECT_EQ(SerializeClientMessage(message), RoomMessageReference::MakeClientMessage(message)) << GetClientMessageType(message).data();
        }
        for (const String& text : HardTexts()) {
            CreateRoomMessage create;
            create.game = text;
            create.name = text;
            create.isPublic = true;
            ClientMessage message = create;
            EXPECT_EQ(SerializeClientMessage(message), RoomMessageReference::MakeClientMessage(message));
            JoinRoomMessage join;
            join.code = text;
            join.name = text;
            message = join;
            EXPECT_EQ(SerializeClientMessage(message), RoomMessageReference::MakeClientMessage(message));
        }
        // an options or an action that is not an object is written as it is, and a deep one is cut as on the DOM path
        CreateRoomMessage create;
        create.options = Json::array({1});
        EXPECT_EQ(SerializeClientMessage(create), RoomMessageReference::MakeClientMessage(create));
        ActionMessage action;
        action.action = MakeDeepArray(kDeepLevels);
        EXPECT_EQ(SerializeClientMessage(action), RoomMessageReference::MakeClientMessage(action));
    }

    TEST(RoomProtocolWriterTest, AClientMessageReadsBackToTheValuesItWasWrittenFrom) {
        for (const ClientMessage& message : MakeClientMessages(std::numeric_limits<int64_t>::max())) {
            const auto parsed = ParseClientMessage(SerializeClientMessage(message));
            ASSERT_TRUE(parsed.has_value()) << GetClientMessageType(message).data() << ": " << parsed.error().code;
            ASSERT_EQ(parsed->index(), message.index());
            EXPECT_EQ(SerializeClientMessage(*parsed), SerializeClientMessage(message));
            if (const auto* action = std::get_if<ActionMessage>(&message)) {
                EXPECT_EQ(std::get<ActionMessage>(*parsed).version, action->version);
                EXPECT_EQ(std::get<ActionMessage>(*parsed).action, action->action);
            }
            if (const auto* create = std::get_if<CreateRoomMessage>(&message)) {
                EXPECT_EQ(std::get<CreateRoomMessage>(*parsed).options, create->options);
                EXPECT_EQ(std::get<CreateRoomMessage>(*parsed).isPublic, create->isPublic);
            }
        }
    }

    TEST(RoomProtocolWriterTest, AppendClientMessageKeepsWhatTheStringHolds) {
        String out("kept:");
        AppendClientMessage(out, PingMessage{});
        AppendClientMessage(out, StartMessage{});
        EXPECT_EQ(out, R"(kept:{"type":"ping","v":1}{"type":"start","v":1})");
    }

    TEST(RoomProtocolWriterTest, RepeatedMessagesAllocateNothingAfterTheFirst) {
        const LongMessages messages;
        String out;
        messages.WriteAll(out);  // the first round sizes the scratch chunk and the String
        ExpectNoAllocations([&] { messages.WriteAll(out); });
    }

    TEST(RoomProtocolWriterTest, EveryWriterGivesTheScratchArenaBackAsItFoundIt) {
        const LongMessages messages;
        String out;
        const size_t before = ScratchArena::Get().GetBytesUsed();
        AppendLobbyMessage(out, messages.lobby);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "lobby";
        AppendRoomListMessage(out, messages.list);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "rooms";
        AppendViewMessage(out, kRoomCode, 1, kLongVersion, false, messages.view, messages.actions);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "view";
        AppendClientMessage(out, messages.create);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "create_room";
        EXPECT_EQ(MakeLobbyMessage(messages.lobby), RoomMessageReference::MakeLobbyMessage(messages.lobby));
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "Make";
    }

    TEST(RoomProtocolWriterTest, AnotherThreadWritesTheSameBytesAndKeepsItsOwnScratchArena) {
        if (!Thread::kAvailable) {
            GTEST_SKIP() << "a single-thread build has no second thread";
        }
        const LongMessages messages;
        String mainText;
        messages.WriteAll(mainText);

        String otherText;
        size_t before = 0;
        size_t after = 0;
        size_t afterAgain = 0;
        Thread thread("Test room writers", [&] {
            before = ScratchArena::Get().GetBytesUsed();
            messages.WriteAll(otherText);
            after = ScratchArena::Get().GetBytesUsed();
            messages.WriteAll(otherText);
            afterAgain = ScratchArena::Get().GetBytesUsed();
        });
        thread.join();
        EXPECT_EQ(before, after);
        EXPECT_EQ(before, afterAgain);
        EXPECT_EQ(otherText, mainText);
    }

}  // namespace BECore::Tests
