#include <BECore/Memory/ScratchArena.h>
#include <BECore/Rooms/SeatedAnswersTest.h>
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
        constexpr int kFullSeatCount = 8;

        /// Every escape of JSON, a quote, a backslash, control characters, non-ASCII text and bytes that are not UTF-8
        const Vector<String>& HardTexts() {
            static const Vector<String> texts = {
                String(),
                String("plain"),
                String("quote \" backslash \\ slash /"),
                String("\b\f\n\r\t"),
                String("nul \0 and \x01\x1f\x7f", 13),
                String("caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80"),
                String("bad \xC3( \xFF \xE2\x82 \xED\xA0\x80 end"),
            };
            return texts;
        }

        /// The JSON text of each entry of HardTexts(), quotes included: the escapes of JSON, a character that is not UTF-8 replaced by U+FFFD
        const Vector<StringView>& HardTextsWritten() {
            static const Vector<StringView> texts = {
                R"("")",
                R"("plain")",
                R"("quote \" backslash \\ slash /")",
                R"("\b\f\n\r\t")",
                "\"nul \\u0000 and \\u0001\\u001f\x7F\"",
                "\"caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x98\x80\"",
                "\"bad \xEF\xBF\xBD( \xEF\xBF\xBD \xEF\xBF\xBD \xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD end\"",
            };
            return texts;
        }

        /// @p pattern with each `@` replaced by @p text: the text of a message that carries one text in several places
        String Fill(StringView pattern, StringView text) {
            String result;
            for (const char c : pattern) {
                if (c == '@') {
                    result.append(text.data(), text.size());
                } else {
                    result.push_back(c);
                }
            }
            return result;
        }

        /// A document with every kind of number and a nested empty object and array, as the text a game would hand over (the keys in byte order)
        constexpr StringView kHardDocument = R"({"big":1e+300,"negative":-9223372036854775808,"nested":{"a":[1,2,{}],"b":{}},"null":null,"real":0.1,"unsigned":18446744073709551615})";

        /// The same with a string of invalid UTF-8 in it (the text a game would hand over has the bytes replaced already)
        constexpr StringView kHardDocumentWithText =
            "{\"badText\":\"bad \xEF\xBF\xBD( "
            "\xEF\xBF\xBD\",\"big\":1e+300,\"negative\":-9223372036854775808,\"nested\":{\"a\":[1,2,{}],\"b\":{}},\"null\":null,\"real\":0.1,\"unsigned\":18446744073709551615}";

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
            String view = R"({"hand":[1,2,3],"table":"a text of more than twenty-three bytes"})";
            String actions = R"([{"pass":true},{"play":1}])";
            ClientMessage create = CreateRoomMessage{String(kLongGame), String(kLongText), R"({"rounds":3})", true};
            ClientMessage join = JoinRoomMessage{String(kRoomCode), String(), String(kLongToken)};
            ClientMessage listRooms = ListRoomsMessage{String(kLongGame)};

            LongMessages() {
                lobby.game = kLongGame;
                lobby.options = R"({"fast":true,"rounds":3})";
                for (LobbySeat& seat : lobby.seats) {
                    if (seat.kind == SeatKind::Human) {
                        seat.name = kLongText;
                    }
                }
                for (RoomListing& listing : list.rooms) {
                    listing.host = kLongText;
                    listing.options = R"({"fast":true})";
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

        EXPECT_EQ(MakeViewMessage(kRoomCode, 1, 5, false, R"({"hand":[1,2],"turn":1})", R"([{"type":"play"}])"),
                  R"({"actions":[{"type":"play"}],"finished":false,"room":"ABCDE","seat":1,"type":"view","v":1,"version":5,"view":{"hand":[1,2],"turn":1}})");

        RoomListMessage list = MakeRoomList(2);
        list.rooms[0].host = "Ann";
        list.rooms[0].taken = 1;
        list.rooms[1].host = "Bob";
        list.rooms[1].taken = 2;
        list.rooms[1].options = R"({"fast":true})";
        EXPECT_EQ(MakeRoomListMessage(list), R"({"game":"cards","more":false,"rooms":[{"host":"Ann","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Bob","options":{"fast":true},"room":"ABCDE","seats":4,"started":true,"taken":2}],"type":"rooms","v":1})");

        EXPECT_EQ(MakeErrorMessage("bad_message", "no", "start"), R"({"code":"bad_message","message":"no","ref":"start","type":"error","v":1})");
        EXPECT_EQ(MakeErrorMessage("bad_message", "no"), R"({"code":"bad_message","message":"no","type":"error","v":1})");
        EXPECT_EQ(MakeLeftMessage("kicked"), R"({"reason":"kicked","type":"left","v":1})");
        EXPECT_EQ(MakePongMessage(), R"({"type":"pong","v":1})");
    }

    TEST(RoomProtocolWriterTest, ALobbyIsWrittenForEveryOptionsShapeAndSeatCount) {
        EXPECT_EQ(MakeLobbyMessage(MakeLobby(0)), R"({"game":"cards","host":0,"phase":"playing","room":"ABCDE","seats":[],"type":"lobby","v":1})");
        EXPECT_EQ(MakeLobbyMessage(MakeLobby(1)),
                  R"({"game":"cards","host":0,"phase":"playing","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Player 0","seat":0}],"type":"lobby","v":1})");
        EXPECT_EQ(MakeLobbyMessage(MakeLobby(kFullSeatCount)), R"({"game":"cards","host":0,"phase":"playing","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Player 0","seat":0},)"
                                                               R"({"kind":"computer","seat":1},{"kind":"empty","seat":2},{"connected":false,"kind":"human","name":"Player 3","seat":3},)"
                                                               R"({"kind":"computer","seat":4},{"kind":"empty","seat":5},{"connected":true,"kind":"human","name":"Player 6","seat":6},)"
                                                               R"({"kind":"computer","seat":7}],"type":"lobby","v":1})");

        LobbySnapshot lobby = MakeLobby(3);
        lobby.options = R"({"fast":true,"rounds":3})";
        EXPECT_EQ(MakeLobbyMessage(lobby),
                  R"({"game":"cards","host":0,"options":{"fast":true,"rounds":3},"phase":"playing","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Player 0","seat":0},)"
                  R"({"kind":"computer","seat":1},{"kind":"empty","seat":2}],"type":"lobby","v":1})");

        // no options, or an empty object, is left out
        for (const char* options : {"", "{}"}) {
            lobby.options = options;
            EXPECT_EQ(MakeLobbyMessage(lobby), R"({"game":"cards","host":0,"phase":"playing","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Player 0","seat":0},)"
                                               R"({"kind":"computer","seat":1},{"kind":"empty","seat":2}],"type":"lobby","v":1})")
                << options;
        }
        lobby.phase = RoomPhase::Finished;
        lobby.hostSeat = -1;
        EXPECT_EQ(MakeLobbyMessage(lobby), R"({"game":"cards","host":-1,"phase":"finished","room":"ABCDE","seats":[{"connected":true,"kind":"human","name":"Player 0","seat":0},)"
                                           R"({"kind":"computer","seat":1},{"kind":"empty","seat":2}],"type":"lobby","v":1})");
    }

    TEST(RoomProtocolWriterTest, TextsWithEscapesAndInvalidUtf8AreWrittenEscaped) {
        ASSERT_EQ(HardTexts().size(), HardTextsWritten().size());
        for (size_t i = 0; i < HardTexts().size(); ++i) {
            const String& text = HardTexts()[i];
            const StringView written = HardTextsWritten()[i];
            SCOPED_TRACE(i);

            LobbySnapshot lobby = MakeLobby(kFullSeatCount);
            lobby.room = text;
            lobby.game = text;
            for (LobbySeat& seat : lobby.seats) {
                seat.name = text;
            }
            EXPECT_EQ(MakeLobbyMessage(lobby), Fill(R"({"game":@,"host":0,"phase":"playing","room":@,"seats":[{"connected":true,"kind":"human","name":@,"seat":0},)"
                                                    R"({"kind":"computer","seat":1},{"kind":"empty","seat":2},{"connected":false,"kind":"human","name":@,"seat":3},)"
                                                    R"({"kind":"computer","seat":4},{"kind":"empty","seat":5},{"connected":true,"kind":"human","name":@,"seat":6},)"
                                                    R"({"kind":"computer","seat":7}],"type":"lobby","v":1})",
                                                    written));

            RoomListMessage list = MakeRoomList(2);
            list.game = text;
            list.rooms[0].room = text;
            list.rooms[1].host = text;
            EXPECT_EQ(MakeRoomListMessage(list), Fill(R"({"game":@,"more":false,"rooms":[{"host":"Host 0","room":@,"seats":4,"started":false,"taken":0},)"
                                                      R"({"host":@,"room":"ABCDE","seats":4,"started":true,"taken":1}],"type":"rooms","v":1})",
                                                      written));

            EXPECT_EQ(MakeJoinedMessage(text, 1, text, 0, true), Fill(R"({"host":true,"player":1,"room":@,"seat":0,"token":@,"type":"joined","v":1})", written));
            // an empty reference is left out
            EXPECT_EQ(MakeErrorMessage(text, text, text),
                      Fill(text.empty() ? StringView(R"({"code":@,"message":@,"type":"error","v":1})") : StringView(R"({"code":@,"message":@,"ref":@,"type":"error","v":1})"), written));
            EXPECT_EQ(MakeLeftMessage(text), Fill(R"({"reason":@,"type":"left","v":1})", written));
            EXPECT_EQ(MakeViewMessage(text, 0, 1, false, written, Format("[{}]", written)),
                      Fill(R"({"actions":[@],"finished":false,"room":@,"seat":0,"type":"view","v":1,"version":1,"view":@})", written));
        }
    }

    TEST(RoomProtocolWriterTest, ARoomListIsWrittenForEveryCountAndOptionsShape) {
        EXPECT_EQ(MakeRoomListMessage(MakeRoomList(0)), R"({"game":"cards","more":false,"rooms":[],"type":"rooms","v":1})");
        RoomListMessage list = MakeRoomList(20);
        list.more = true;
        EXPECT_EQ(MakeRoomListMessage(list), R"({"game":"cards","more":true,"rooms":[{"host":"Host 0","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 1","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 2","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 3","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 4","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 5","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 6","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 7","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 8","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 9","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 10","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 11","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 12","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 13","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 14","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 15","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 16","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 17","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 18","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 19","room":"ABCDE","seats":4,"started":true,"taken":1}],"type":"rooms","v":1})");
        list.rooms[0].options = R"({"fast":true})";
        list.rooms[1].options = "{}";
        list.rooms[2].options = "";
        EXPECT_EQ(MakeRoomListMessage(list), R"({"game":"cards","more":true,"rooms":[{"host":"Host 0","options":{"fast":true},"room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 1","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 2","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 3","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 4","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 5","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 6","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 7","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 8","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 9","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 10","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 11","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 12","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 13","room":"ABCDE","seats":4,"started":true,"taken":1},)"
                                             R"({"host":"Host 14","room":"ABCDE","seats":4,"started":false,"taken":2},)"
                                             R"({"host":"Host 15","room":"ABCDE","seats":4,"started":true,"taken":0},)"
                                             R"({"host":"Host 16","room":"ABCDE","seats":4,"started":false,"taken":1},)"
                                             R"({"host":"Host 17","room":"ABCDE","seats":4,"started":true,"taken":2},)"
                                             R"({"host":"Host 18","room":"ABCDE","seats":4,"started":false,"taken":0},)"
                                             R"({"host":"Host 19","room":"ABCDE","seats":4,"started":true,"taken":1}],"type":"rooms","v":1})");
    }

    TEST(RoomProtocolWriterTest, AViewCarriesTheGamesJsonAsItIs) {
        const String hardActions = Format("[{},{}]", kHardDocumentWithText, "{}");
        EXPECT_EQ(MakeViewMessage(kRoomCode, 3, 9, true, kHardDocumentWithText, hardActions),
                  "{\"actions\":[{\"badText\":\"bad \xEF\xBF\xBD( "
                  "\xEF\xBF\xBD\",\"big\":1e+300,\"negative\":-9223372036854775808,\"nested\":{\"a\":[1,2,{}],\"b\":{}},\"null\":null,\"real\":0.1,\"unsigned\":18446744073709551615},"
                  "{}],\"finished\":true,\"room\":\"ABCDE\",\"seat\":3,\"type\":\"view\",\"v\":1,\"version\":9,\"view\":{\"badText\":\"bad \xEF\xBF\xBD( "
                  "\xEF\xBF\xBD\",\"big\":1e+300,\"negative\":-9223372036854775808,\"nested\":{\"a\":[1,2,{}],\"b\":{}},\"null\":null,\"real\":0.1,\"unsigned\":18446744073709551615}}");
        EXPECT_EQ(MakeViewMessage(kRoomCode, 0, 0, false, "{}", "[]"), R"({"actions":[],"finished":false,"room":"ABCDE","seat":0,"type":"view","v":1,"version":0,"view":{}})");
        // a view and actions of another shape than the contract says are written as they are
        EXPECT_EQ(MakeViewMessage(kRoomCode, 0, 0, false, "null", "{}"), R"({"actions":{},"finished":false,"room":"ABCDE","seat":0,"type":"view","v":1,"version":0,"view":null})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, 0, 0, false, "[1]", "5"), R"({"actions":5,"finished":false,"room":"ABCDE","seat":0,"type":"view","v":1,"version":0,"view":[1]})");
    }

    TEST(RoomProtocolWriterTest, TheTextOverloadOfAViewWritesEachValueAsGiven) {
        EXPECT_EQ(MakeViewMessage(kRoomCode, 1, 5, false, R"({ "turn" : 1 })", "[ ]"),
                  R"({"actions":[ ],"finished":false,"room":"ABCDE","seat":1,"type":"view","v":1,"version":5,"view":{ "turn" : 1 }})");
    }

    TEST(RoomProtocolWriterTest, ATypedViewIsTheSameBytesAsTheTextOverloadGivenTheEqualDocument) {
        ASSERT_EQ(HardTexts().size(), HardTextsWritten().size());
        for (size_t i = 0; i < HardTexts().size(); ++i) {
            const String& name = HardTexts()[i];
            const StringView written = HardTextsWritten()[i];
            SCOPED_TRACE(i);
            AnswerView view;
            view.name = name;
            view.turn = 4;
            SeatedActionList<AnswerAction> actions;
            EXPECT_EQ(MakeViewMessage(kRoomCode, 2, 9, false, view, actions),
                      Fill(R"({"actions":[],"finished":false,"room":"ABCDE","seat":2,"type":"view","v":1,"version":9,"view":{"name":@,"points":[],"turn":4}})", written))
                << "an empty list stays an array";

            for (int32_t n = 0; n < 2; ++n) {
                AnswerPoint& point = view.points.emplace_back();
                point.x = n;
                point.y = -n;
                AnswerAction action;
                action.card = n + 10;
                action.type = name;
                actions.Add(std::move(action));
            }
            const String expected = Fill(
                R"({"actions":[{"card":10,"type":@},{"card":11,"type":@}],"finished":true,"room":"ABCDE","seat":2,"type":"view","v":1,"version":9,"view":{"name":@,"points":[{"x":0,"y":0},{"x":1,"y":-1}],"turn":4}})",
                written);
            EXPECT_EQ(MakeViewMessage(kRoomCode, 2, 9, true, view, actions), expected);

            String appended = "kept:";
            const size_t scratchBefore = ScratchArena::Get().GetBytesUsed();
            AppendViewMessage(appended, kRoomCode, 2, 9, true, view, actions);
            EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), scratchBefore);
            EXPECT_EQ(appended, Format("kept:{}", expected));
        }
    }

    TEST(RoomProtocolWriterTest, TheNumbersOfAMessageKeepTheirLimits) {
        constexpr uint64_t kMaxVersion = std::numeric_limits<uint64_t>::max();
        constexpr PlayerId kMaxPlayer = std::numeric_limits<PlayerId>::max();
        constexpr int kMinSeat = std::numeric_limits<int>::min();
        constexpr int kMaxSeat = std::numeric_limits<int>::max();
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMinSeat, 0, false, "{}", "[]"), R"({"actions":[],"finished":false,"room":"ABCDE","seat":-2147483648,"type":"view","v":1,"version":0,"view":{}})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMaxSeat, 0, true, "{}", "[]"), R"({"actions":[],"finished":true,"room":"ABCDE","seat":2147483647,"type":"view","v":1,"version":0,"view":{}})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMinSeat, std::numeric_limits<int64_t>::max(), false, "{}", "[]"),
                  R"({"actions":[],"finished":false,"room":"ABCDE","seat":-2147483648,"type":"view","v":1,"version":9223372036854775807,"view":{}})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMaxSeat, std::numeric_limits<int64_t>::max(), true, "{}", "[]"),
                  R"({"actions":[],"finished":true,"room":"ABCDE","seat":2147483647,"type":"view","v":1,"version":9223372036854775807,"view":{}})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMinSeat, kMaxVersion, false, "{}", "[]"),
                  R"({"actions":[],"finished":false,"room":"ABCDE","seat":-2147483648,"type":"view","v":1,"version":18446744073709551615,"view":{}})");
        EXPECT_EQ(MakeViewMessage(kRoomCode, kMaxSeat, kMaxVersion, true, "{}", "[]"),
                  R"({"actions":[],"finished":true,"room":"ABCDE","seat":2147483647,"type":"view","v":1,"version":18446744073709551615,"view":{}})");
        EXPECT_EQ(MakeJoinedMessage(kRoomCode, kMaxPlayer, kToken, kMinSeat, true),
                  R"({"host":true,"player":4294967295,"room":"ABCDE","seat":-2147483648,"token":"0123456789abcdef0123456789abcdef","type":"joined","v":1})");
        EXPECT_EQ(MakeJoinedMessage(kRoomCode, 0, kToken, kMaxSeat, false),
                  R"({"host":false,"player":0,"room":"ABCDE","seat":2147483647,"token":"0123456789abcdef0123456789abcdef","type":"joined","v":1})");
    }

    TEST(RoomProtocolWriterTest, AppendKeepsWhatTheStringHoldsAndTheMakeFunctionsReturnTheSameBytes) {
        const LobbySnapshot lobby = MakeLobby(3);
        const RoomListMessage list = MakeRoomList(2);
        constexpr StringView kView = R"({"hand":[1,2]})";
        constexpr StringView kActions = "[1]";

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
            {MakeViewMessage(kRoomCode, 1, 5, false, kView, kActions),
             [&] {
                 String out;
                 AppendViewMessage(out, kRoomCode, 1, 5, false, kView, kActions);
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
        lobby.options = R"({"fast":true,"rounds":3})";
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
        constexpr StringView kView = R"({"big":1e+300,"hand":[1,2],"unsigned":18446744073709551615})";
        constexpr StringView kActions = R"([{"type":"play"}])";
        const auto viewParsed = ParseServerMessage(MakeViewMessage(kRoomCode, 4, kMaxReadableVersion, true, kView, kActions));
        ASSERT_TRUE(viewParsed.has_value());
        const auto* viewMessage = std::get_if<ViewMessage>(&*viewParsed);
        ASSERT_NE(viewMessage, nullptr);
        EXPECT_EQ(viewMessage->seat, 4);
        EXPECT_EQ(viewMessage->version, kMaxReadableVersion);
        EXPECT_TRUE(viewMessage->finished);
        EXPECT_EQ(viewMessage->view, kView);
        EXPECT_EQ(viewMessage->actions, kActions);

        RoomListMessage list = MakeRoomList(3);
        list.more = true;
        list.rooms[1].options = R"({"fast":true})";
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
                message.options = String(kHardDocument);
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
            action.action = String(kHardDocument);
            ActionMessage smallAction;
            smallAction.version = 3;
            smallAction.action = R"({"card":4})";
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
        create.options = R"({"fast":true})";
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
        action.action = R"({"card":4})";
        EXPECT_EQ(SerializeClientMessage(action), R"({"action":{"card":4},"type":"action","v":1,"version":7})");
        EXPECT_EQ(SerializeClientMessage(LeaveMessage{}), R"({"type":"leave","v":1})");
        EXPECT_EQ(SerializeClientMessage(PingMessage{}), R"({"type":"ping","v":1})");
        ListRoomsMessage list;
        list.game = "cards";
        EXPECT_EQ(SerializeClientMessage(list), R"({"game":"cards","type":"list_rooms","v":1})");
    }

    TEST(RoomProtocolWriterTest, ClientMessagesAreWrittenForEveryKindAndText) {
        const Vector<ClientMessage> messages = MakeClientMessages(std::numeric_limits<uint64_t>::max());
        const Vector<StringView> expected = {
            R"({"game":"cards","name":"Ann","options":{},"type":"create_room","v":1})",
            R"({"game":"cards","name":"Ann","options":{"big":1e+300,"negative":-9223372036854775808,"nested":{"a":[1,2,{}],"b":{}},"null":null,"real":0.1,"unsigned":18446744073709551615},"public":true,"type":"create_room","v":1})",
            R"({"code":"ABCDE","name":"Bob","type":"join_room","v":1})",
            R"({"code":"ABCDE","token":"0123456789abcdef0123456789abcdef","type":"join_room","v":1})",
            R"({"seat":-9223372036854775808,"type":"choose_seat","v":1})",
            R"({"seat":2,"type":"choose_seat","v":1})",
            R"({"type":"start","v":1})",
            R"({"action":{"big":1e+300,"negative":-9223372036854775808,"nested":{"a":[1,2,{}],"b":{}},"null":null,"real":0.1,"unsigned":18446744073709551615},"type":"action","v":1,"version":18446744073709551615})",
            R"({"action":{"card":4},"type":"action","v":1,"version":3})",
            R"({"type":"leave","v":1})",
            R"({"type":"ping","v":1})",
            R"({"game":"cards","type":"list_rooms","v":1})",
        };
        ASSERT_EQ(messages.size(), expected.size());
        for (size_t i = 0; i < messages.size(); ++i) {
            EXPECT_EQ(SerializeClientMessage(messages[i]), expected[i]) << GetClientMessageType(messages[i]).data();
        }
        ASSERT_EQ(HardTexts().size(), HardTextsWritten().size());
        for (size_t i = 0; i < HardTexts().size(); ++i) {
            const String& text = HardTexts()[i];
            SCOPED_TRACE(i);
            CreateRoomMessage create;
            create.game = text;
            create.name = text;
            create.isPublic = true;
            EXPECT_EQ(SerializeClientMessage(create), Fill(R"({"game":@,"name":@,"options":{},"public":true,"type":"create_room","v":1})", HardTextsWritten()[i]));
            JoinRoomMessage join;
            join.code = text;
            join.name = text;
            EXPECT_EQ(SerializeClientMessage(join), Fill(R"({"code":@,"name":@,"type":"join_room","v":1})", HardTextsWritten()[i]));
        }
        // an options or an action text is written as it is, and an empty one as the empty object
        CreateRoomMessage create;
        create.options = "[1]";
        EXPECT_EQ(SerializeClientMessage(create), R"({"game":"","name":"","options":[1],"type":"create_room","v":1})");
        EXPECT_NE(SerializeClientMessage(CreateRoomMessage{}).find(R"("options":{})"), String::npos);
        ActionMessage action;
        EXPECT_NE(SerializeClientMessage(action).find(R"("action":{})"), String::npos);
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
                EXPECT_EQ(std::get<CreateRoomMessage>(*parsed).options, create->options.empty() ? String("{}") : create->options);
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
        String made = MakeLobbyMessage(messages.lobby);
        EXPECT_EQ(ScratchArena::Get().GetBytesUsed(), before) << "Make";
        String appended;
        AppendLobbyMessage(appended, messages.lobby);
        EXPECT_EQ(made, appended);
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
