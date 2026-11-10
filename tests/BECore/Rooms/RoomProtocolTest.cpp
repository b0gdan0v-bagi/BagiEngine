#include <Common/Printers.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomProtocol.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr eastl::string_view kPlainPing = R"({"v":1,"type":"ping"})";

        eastl::string ErrorCodeOf(const std::expected<ClientMessage, ControlError>& parsed) {
            return parsed ? eastl::string("<ok>") : parsed.error().code;
        }

        eastl::string ErrorCodeOfText(eastl::string_view text, size_t maxBytes = RoomLimits::kMaxMessageBytes) {
            return ErrorCodeOf(ParseClientMessage(text, maxBytes));
        }

        eastl::string Repeat(char c, size_t count) {
            return eastl::string(count, c);
        }

        /// A message of @p type with the members of @p members (a JSON object text without braces, may be empty)
        eastl::string Message(eastl::string_view type, eastl::string_view members = {}) {
            eastl::string text = Format(R"({{"v":1,"type":"{}")", type);
            if (!members.empty()) {
                text.append(",");
                text.append(members.data(), members.size());
            }
            text.append("}");
            return text;
        }

        Json ParseOrNull(const eastl::string& text) {
            auto parsed = ParseJson(text);
            return parsed ? *parsed : Json();
        }

        template <typename T>
        const T* As(const std::expected<ClientMessage, ControlError>& parsed) {
            if (!parsed) {
                return nullptr;
            }
            return std::get_if<T>(&*parsed);
        }

        struct BadInput {
            const char* label;
            eastl::string text;
            const char* code;
        };

    }  // namespace

    static_assert(kRoomProtocolVersion == 1);
    static_assert(std::variant_size_v<ClientMessage> == 7);

    TEST(RoomProtocolTest, CompileTimeChecks) {
        static_assert(std::is_same_v<std::variant_alternative_t<0, ClientMessage>, CreateRoomMessage>);
        SUCCEED();
    }

    // ---- Client messages: parse ---------------------------------------------------------------------------------------------------

    TEST(RoomProtocolTest, CreateRoomParses) {
        const auto parsed = ParseClientMessage(R"({"v":1,"type":"create_room","game":"mahjong","name":"  Ann  ","options":{"rounds":2}})");
        const auto* message = As<CreateRoomMessage>(parsed);
        ASSERT_NE(message, nullptr);
        EXPECT_EQ(message->game, "mahjong");
        EXPECT_EQ(message->name, "Ann");
        EXPECT_EQ(GetJsonInt(message->options, "rounds"), 2);
    }

    TEST(RoomProtocolTest, CreateRoomWithoutOptionsHasAnEmptyObject) {
        const auto parsed = ParseClientMessage(Message("create_room", R"("game":"g","name":"Bob")"));
        const auto* message = As<CreateRoomMessage>(parsed);
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->options.is_object());
        EXPECT_TRUE(message->options.empty());
    }

    TEST(RoomProtocolTest, CreateRoomOptionsMustBeAnObject) {
        for (const char* options : {"null", "[]", "[1]", "\"x\"", "5", "true", "1.5"}) {
            const eastl::string text = Message("create_room", Format(R"("game":"g","name":"Bob","options":{})", options));
            EXPECT_EQ(ErrorCodeOfText(text), RoomErrors::kBadMessage) << options;
        }
    }

    TEST(RoomProtocolTest, JoinByCodeNormalisesTheCode) {
        const auto parsed = ParseClientMessage(R"({"v":1,"type":"join_room","code":"k7m2x","name":"Bob"})");
        const auto* message = As<JoinRoomMessage>(parsed);
        ASSERT_NE(message, nullptr);
        EXPECT_EQ(message->code, "K7M2X");
        EXPECT_EQ(message->name, "Bob");
        EXPECT_FALSE(message->IsReconnect());
    }

    TEST(RoomProtocolTest, JoinByTokenIsAReconnect) {
        const auto parsed = ParseClientMessage(R"({"v":1,"type":"join_room","code":"K7M2X","token":"0123456789abcdef0123456789abcdef"})");
        const auto* message = As<JoinRoomMessage>(parsed);
        ASSERT_NE(message, nullptr);
        EXPECT_TRUE(message->IsReconnect());
        EXPECT_EQ(message->token, "0123456789abcdef0123456789abcdef");
        EXPECT_TRUE(message->name.empty());
    }

    TEST(RoomProtocolTest, ChooseSeatKeepsAnyInteger) {
        for (const int64_t seat : {int64_t{0}, int64_t{3}, int64_t{-1}, int64_t{999999999999}}) {
            const auto parsed = ParseClientMessage(Message("choose_seat", Format(R"("seat":{})", seat)));
            const auto* message = As<ChooseSeatMessage>(parsed);
            ASSERT_NE(message, nullptr) << seat;
            EXPECT_EQ(message->seat, seat);
        }
    }

    TEST(RoomProtocolTest, ActionKeepsTheVersionAndTheObject) {
        const auto parsed = ParseClientMessage(R"({"v":1,"type":"action","version":7,"action":{"type":"play","card":"c1_2"}})");
        const auto* message = As<ActionMessage>(parsed);
        ASSERT_NE(message, nullptr);
        EXPECT_EQ(message->version, 7u);
        EXPECT_EQ(GetJsonString(message->action, "card"), "c1_2");
    }

    TEST(RoomProtocolTest, MessagesWithoutFieldsParse) {
        EXPECT_NE(As<StartMessage>(ParseClientMessage(Message("start"))), nullptr);
        EXPECT_NE(As<LeaveMessage>(ParseClientMessage(Message("leave"))), nullptr);
        EXPECT_NE(As<PingMessage>(ParseClientMessage(kPlainPing)), nullptr);
    }

    TEST(RoomProtocolTest, UnknownMembersAreIgnored) {
        EXPECT_NE(As<PingMessage>(ParseClientMessage(R"({"v":1,"type":"ping","extra":[1,2,{"a":null}],"seat":"x"})")), nullptr);
    }

    // ---- Client messages: round trip ----------------------------------------------------------------------------------------------

    TEST(RoomProtocolTest, EveryClientMessageRoundTrips) {
        CreateRoomMessage create;
        create.game = "mahjong";
        create.name = "Ann";
        create.options = Json::object();
        create.options["rounds"] = 4;
        JoinRoomMessage joinByName;
        joinByName.code = "ABCDE";
        joinByName.name = "Bob";
        JoinRoomMessage reconnect;
        reconnect.code = "ABCDE";
        reconnect.token = "00112233445566778899aabbccddeeff";
        ActionMessage action;
        action.version = 12;
        action.action = Json::object();
        action.action["type"] = "play";

        const eastl::vector<ClientMessage> messages = {
            create, joinByName, reconnect, ChooseSeatMessage{2}, StartMessage{}, action, LeaveMessage{}, PingMessage{},
        };
        for (const ClientMessage& original : messages) {
            const eastl::string text = SerializeClientMessage(original);
            const auto parsed = ParseClientMessage(text);
            ASSERT_TRUE(parsed.has_value()) << text.c_str();
            EXPECT_EQ(parsed->index(), original.index()) << text.c_str();
            EXPECT_EQ(GetClientMessageType(*parsed), GetClientMessageType(original));
            EXPECT_EQ(SerializeClientMessage(*parsed), text);
        }
    }

    TEST(RoomProtocolTest, ClientMessageTypeNames) {
        EXPECT_EQ(GetClientMessageType(CreateRoomMessage{}), "create_room");
        EXPECT_EQ(GetClientMessageType(JoinRoomMessage{}), "join_room");
        EXPECT_EQ(GetClientMessageType(ChooseSeatMessage{}), "choose_seat");
        EXPECT_EQ(GetClientMessageType(StartMessage{}), "start");
        EXPECT_EQ(GetClientMessageType(ActionMessage{}), "action");
        EXPECT_EQ(GetClientMessageType(LeaveMessage{}), "leave");
        EXPECT_EQ(GetClientMessageType(PingMessage{}), "ping");
    }

    // ---- Client messages: malformed input -----------------------------------------------------------------------------------------

    TEST(RoomProtocolTest, MalformedInputGivesTheStableCodeAndNeverAborts) {
        const eastl::string bomb = Repeat('[', RoomLimits::kMaxMessageBytes);
        eastl::string balancedBomb = Repeat('[', RoomLimits::kMaxMessageBytes / 2);
        balancedBomb.append(RoomLimits::kMaxMessageBytes / 2, ']');
        eastl::string bombInsideAMember = R"({"v":1,"type":"ping","x":)";
        const size_t depth = (RoomLimits::kMaxMessageBytes - bombInsideAMember.size() - 1) / 2;
        bombInsideAMember.append(depth, '[');
        bombInsideAMember.append(depth, ']');
        bombInsideAMember.append("}");
        ASSERT_LE(bombInsideAMember.size(), RoomLimits::kMaxMessageBytes);

        const eastl::string tooLong = Message("ping", Format(R"("pad":"{}")", Repeat('x', RoomLimits::kMaxMessageBytes)));
        const eastl::string invalidUtf8 = Format("{{\"v\":1,\"type\":\"ping\",\"pad\":\"{}\"}}", "\xC3\x28");
        const eastl::string overlong = Format("{{\"v\":1,\"type\":\"ping\",\"pad\":\"{}\"}}", "\xC0\xAF");
        const eastl::string nulByte = eastl::string("{\"v\":1,\"type\":\"ping\"}\0garbage", 24);

        const eastl::vector<BadInput> table = {
            {"empty", "", "bad_message"},
            {"spaces", "   ", "bad_message"},
            {"not json", "hello", "bad_message"},
            {"truncated", R"({"v":1,"type":"ping")", "bad_message"},
            {"trailing garbage", R"({"v":1,"type":"ping"} x)", "bad_message"},
            {"two values", R"({"v":1,"type":"ping"}{"v":1,"type":"ping"})", "bad_message"},
            {"nul after the object", nulByte, "bad_message"},
            {"array", R"([{"v":1,"type":"ping"}])", "bad_message"},
            {"string", R"("ping")", "bad_message"},
            {"number", "1", "bad_message"},
            {"null", "null", "bad_message"},
            {"nesting bomb, unterminated", bomb, "bad_message"},
            {"nesting bomb, balanced", balancedBomb, "bad_message"},
            {"over the limit", tooLong, "message_too_large"},
            {"invalid utf-8", invalidUtf8, "bad_message"},
            {"overlong utf-8", overlong, "bad_message"},
            {"v missing", R"({"type":"ping"})", "bad_message"},
            {"v null", R"({"v":null,"type":"ping"})", "bad_message"},
            {"v string", R"({"v":"1","type":"ping"})", "bad_message"},
            {"v fractional", R"({"v":1.5,"type":"ping"})", "bad_message"},
            {"v float one", R"({"v":1.0,"type":"ping"})", "bad_message"},
            {"v huge", R"({"v":1e300,"type":"ping"})", "bad_message"},
            {"v beyond int64", R"({"v":18446744073709551616,"type":"ping"})", "bad_message"},
            {"v zero", R"({"v":0,"type":"ping"})", "unsupported_version"},
            {"v two", R"({"v":2,"type":"ping"})", "unsupported_version"},
            {"v negative", R"({"v":-1,"type":"ping"})", "unsupported_version"},
            {"type missing", R"({"v":1})", "bad_message"},
            {"type null", R"({"v":1,"type":null})", "bad_message"},
            {"type number", R"({"v":1,"type":5})", "bad_message"},
            {"type unknown", R"({"v":1,"type":"explode"})", "bad_message"},
            {"type empty", R"({"v":1,"type":""})", "bad_message"},
            {"type in other case", R"({"v":1,"type":"PING"})", "bad_message"},
            {"server type from a client", R"({"v":1,"type":"view"})", "bad_message"},
            {"create: game missing", Message("create_room", R"("name":"Ann")"), "bad_message"},
            {"create: game number", Message("create_room", R"("game":5,"name":"Ann")"), "bad_message"},
            {"create: game null", Message("create_room", R"("game":null,"name":"Ann")"), "bad_message"},
            {"create: game empty", Message("create_room", R"("game":"","name":"Ann")"), "bad_message"},
            {"create: game too long", Message("create_room", Format(R"("game":"{}","name":"Ann")", Repeat('g', RoomLimits::kMaxGameNameBytes + 1))), "bad_message"},
            {"create: name missing", Message("create_room", R"("game":"g")"), "bad_message"},
            {"create: name number", Message("create_room", R"("game":"g","name":7)"), "bad_message"},
            {"create: name null", Message("create_room", R"("game":"g","name":null)"), "bad_message"},
            {"create: name object", Message("create_room", R"("game":"g","name":{})"), "bad_message"},
            {"create: name empty", Message("create_room", R"("game":"g","name":"")"), "bad_name"},
            {"create: name spaces", Message("create_room", R"("game":"g","name":"    ")"), "bad_name"},
            {"create: name 25 bytes", Message("create_room", Format(R"("game":"g","name":"{}")", Repeat('n', 25))), "bad_name"},
            {"create: name with a bell", Message("create_room", R"("game":"g","name":"a\u0007b")"), "bad_name"},
            {"create: name with a newline", Message("create_room", R"("game":"g","name":"a\nb")"), "bad_name"},
            {"create: name with a tab at the end", Message("create_room", R"("game":"g","name":"ab\t")"), "bad_name"},
            {"create: name with a nul", Message("create_room", R"("game":"g","name":"a\u0000b")"), "bad_name"},
            {"create: name with DEL", Message("create_room", R"("game":"g","name":"a\u007fb")"), "bad_name"},
            {"create: name with a C1 control", Message("create_room", R"("game":"g","name":"a\u0085b")"), "bad_name"},
            {"create: name with a direction override", Message("create_room", R"("game":"g","name":"a\u202eb")"), "bad_name"},
            {"create: name with a line separator", Message("create_room", R"("game":"g","name":"a\u2028b")"), "bad_name"},
            {"create: name with a zero-width space", Message("create_room", R"("game":"g","name":"a\u200bb")"), "bad_name"},
            {"create: name with a lone surrogate", Message("create_room", R"("game":"g","name":"a\ud800b")"), "bad_message"},
            {"create: name as 13 two-byte characters",
             Message("create_room", Format(R"("game":"g","name":"{}")", "\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9\xC3\xA9")), "bad_name"},
            {"join: code missing", Message("join_room", R"("name":"Bob")"), "bad_message"},
            {"join: code number", Message("join_room", R"("code":12345,"name":"Bob")"), "bad_message"},
            {"join: code null", Message("join_room", R"("code":null,"name":"Bob")"), "bad_message"},
            {"join: neither name nor token", Message("join_room", R"("code":"ABCDE")"), "bad_message"},
            {"join: both name and token", Message("join_room", R"("code":"ABCDE","name":"Bob","token":"0123456789abcdef0123456789abcdef")"), "bad_message"},
            {"join: name null", Message("join_room", R"("code":"ABCDE","name":null)"), "bad_message"},
            {"join: token number", Message("join_room", R"("code":"ABCDE","token":5)"), "bad_message"},
            {"join: code too short", Message("join_room", R"("code":"ABCD","name":"Bob")"), "unknown_room"},
            {"join: code too long", Message("join_room", R"("code":"ABCDEF","name":"Bob")"), "unknown_room"},
            {"join: code with I", Message("join_room", R"("code":"ABCDI","name":"Bob")"), "unknown_room"},
            {"join: code with zero", Message("join_room", R"("code":"ABCD0","name":"Bob")"), "unknown_room"},
            {"join: code empty", Message("join_room", R"("code":"","name":"Bob")"), "unknown_room"},
            {"join: code with a space", Message("join_room", R"("code":"AB CD","name":"Bob")"), "unknown_room"},
            {"join: token too short", Message("join_room", R"("code":"ABCDE","token":"0123")"), "bad_token"},
            {"join: token upper case", Message("join_room", R"("code":"ABCDE","token":"0123456789ABCDEF0123456789ABCDEF")"), "bad_token"},
            {"join: token not hex", Message("join_room", R"("code":"ABCDE","token":"0123456789abcdef0123456789abcdeg")"), "bad_token"},
            {"join: token empty", Message("join_room", R"("code":"ABCDE","token":"")"), "bad_token"},
            {"seat missing", Message("choose_seat"), "bad_message"},
            {"seat string", Message("choose_seat", R"("seat":"3")"), "bad_message"},
            {"seat null", Message("choose_seat", R"("seat":null)"), "bad_message"},
            {"seat fractional", Message("choose_seat", R"("seat":1.5)"), "bad_message"},
            {"seat float integer", Message("choose_seat", R"("seat":2.0)"), "bad_message"},
            {"seat huge", Message("choose_seat", R"("seat":1e30)"), "bad_message"},
            {"seat beyond int64", Message("choose_seat", R"("seat":18446744073709551615)"), "bad_message"},
            {"seat bool", Message("choose_seat", R"("seat":true)"), "bad_message"},
            {"seat array", Message("choose_seat", R"("seat":[1])"), "bad_message"},
            {"action: version missing", Message("action", R"("action":{})"), "bad_message"},
            {"action: version negative", Message("action", R"("version":-1,"action":{})"), "bad_message"},
            {"action: version fractional", Message("action", R"("version":1.5,"action":{})"), "bad_message"},
            {"action: version string", Message("action", R"("version":"3","action":{})"), "bad_message"},
            {"action: version beyond int64", Message("action", R"("version":9223372036854775808,"action":{})"), "bad_message"},
            {"action: action missing", Message("action", R"("version":1)"), "bad_message"},
            {"action: action null", Message("action", R"("version":1,"action":null)"), "bad_message"},
            {"action: action array", Message("action", R"("version":1,"action":[])"), "bad_message"},
            {"action: action string", Message("action", R"("version":1,"action":"play")"), "bad_message"},
        };
        for (const BadInput& input : table) {
            EXPECT_EQ(ErrorCodeOfText(input.text), eastl::string(input.code)) << input.label;
        }
        // The deep array inside an ignored member is fine as long as it is valid JSON under the limit
        EXPECT_EQ(ErrorCodeOfText(bombInsideAMember), "<ok>");
    }

    TEST(RoomProtocolTest, TheSizeLimitIsCheckedBeforeParsing) {
        // Not JSON at all, but over the limit: the size is the answer
        EXPECT_EQ(ErrorCodeOfText(Repeat('x', 100), 99), RoomErrors::kMessageTooLarge);
        EXPECT_EQ(ErrorCodeOfText(Repeat('x', 100), 100), RoomErrors::kBadMessage);
        EXPECT_EQ(ErrorCodeOfText(kPlainPing, kPlainPing.size()), "<ok>");
        EXPECT_EQ(ErrorCodeOfText(kPlainPing, kPlainPing.size() - 1), RoomErrors::kMessageTooLarge);
    }

    TEST(RoomProtocolTest, EveryByteValueAsAMessageNeverAborts) {
        for (int byte = 0; byte < 256; ++byte) {
            const eastl::string one(1, static_cast<char>(byte));
            EXPECT_FALSE(ParseClientMessage(one).has_value()) << byte;
            const eastl::string inside = Format("{{\"v\":1,\"type\":\"join_room\",\"code\":\"{}\",\"name\":\"{}\"}}", one, one);
            const auto parsedInside = ParseClientMessage(inside);
            EXPECT_TRUE(parsedInside.has_value() || !parsedInside.error().code.empty()) << byte;
        }
        SUCCEED();
    }

    // ---- Names --------------------------------------------------------------------------------------------------------------------

    TEST(RoomProtocolTest, SanitizePlayerNameTrimsAndKeepsPrintableText) {
        EXPECT_EQ(SanitizePlayerName("Ann"), "Ann");
        EXPECT_EQ(SanitizePlayerName("  Ann Lee  "), "Ann Lee");
        EXPECT_EQ(SanitizePlayerName("\xD0\x90\xD0\xBD\xD1\x8F"), "\xD0\x90\xD0\xBD\xD1\x8F");  // Cyrillic, 6 bytes
        EXPECT_EQ(SanitizePlayerName(Repeat('a', RoomLimits::kMaxNameBytes)), Repeat('a', RoomLimits::kMaxNameBytes));
        EXPECT_EQ(SanitizePlayerName(Format("  {}  ", Repeat('a', RoomLimits::kMaxNameBytes))), Repeat('a', RoomLimits::kMaxNameBytes));
        EXPECT_EQ(SanitizePlayerName("a"), "a");
    }

    TEST(RoomProtocolTest, SanitizePlayerNameRefusesWhatCannotBeAName) {
        EXPECT_FALSE(SanitizePlayerName("").has_value());
        EXPECT_FALSE(SanitizePlayerName("   ").has_value());
        EXPECT_FALSE(SanitizePlayerName(Repeat('a', RoomLimits::kMaxNameBytes + 1)).has_value());
        EXPECT_FALSE(SanitizePlayerName("a\tb").has_value());
        EXPECT_FALSE(SanitizePlayerName("\tab").has_value());
        EXPECT_FALSE(SanitizePlayerName(eastl::string_view("a\0b", 3)).has_value());
        // Invalid UTF-8: a lone continuation byte, a truncated sequence, an overlong form, a surrogate, a value above U+10FFFF
        EXPECT_FALSE(SanitizePlayerName("\x80").has_value());
        EXPECT_FALSE(SanitizePlayerName("ab\xC3").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xC3\x28").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xC0\xAF").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xE0\x80\xAF").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xED\xA0\x80").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xF4\x90\x80\x80").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xF8\x88\x80\x80\x80").has_value());
        EXPECT_FALSE(SanitizePlayerName("\xFF").has_value());
        // A four-byte character that is allowed
        EXPECT_TRUE(SanitizePlayerName("\xF0\x9F\x98\x80").has_value());
    }

    // ---- Server messages ----------------------------------------------------------------------------------------------------------

    TEST(RoomProtocolTest, JoinedMessage) {
        const Json message = ParseOrNull(MakeJoinedMessage("ABCDE", 3, "00112233445566778899aabbccddeeff", 2, true));
        EXPECT_EQ(GetJsonInt(message, "v"), 1);
        EXPECT_EQ(GetJsonString(message, "type"), "joined");
        EXPECT_EQ(GetJsonString(message, "room"), "ABCDE");
        EXPECT_EQ(GetJsonInt(message, "player"), 3);
        EXPECT_EQ(GetJsonString(message, "token"), "00112233445566778899aabbccddeeff");
        EXPECT_EQ(GetJsonInt(message, "seat"), 2);
        EXPECT_EQ(GetJsonBool(message, "host"), true);
    }

    TEST(RoomProtocolTest, LobbyMessage) {
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = "fake";
        lobby.phase = RoomPhase::Lobby;
        lobby.hostSeat = 0;
        lobby.seats.push_back(LobbySeat{0, SeatKind::Human, "Ann", true});
        lobby.seats.push_back(LobbySeat{1, SeatKind::Human, "Bob", false});
        lobby.seats.push_back(LobbySeat{2, SeatKind::Computer, "ignored", true});
        lobby.seats.push_back(LobbySeat{3, SeatKind::Empty, {}, false});

        const Json message = ParseOrNull(MakeLobbyMessage(lobby));
        EXPECT_EQ(GetJsonString(message, "type"), "lobby");
        EXPECT_EQ(GetJsonString(message, "room"), "ABCDE");
        EXPECT_EQ(GetJsonString(message, "game"), "fake");
        EXPECT_EQ(GetJsonString(message, "phase"), "lobby");
        EXPECT_EQ(GetJsonInt(message, "host"), 0);
        const Json* seats = FindJsonMember(message, "seats");
        ASSERT_NE(seats, nullptr);
        ASSERT_TRUE(seats->is_array());
        ASSERT_EQ(seats->size(), 4u);

        const Json& ann = (*seats)[0];
        EXPECT_EQ(GetJsonString(ann, "kind"), "human");
        EXPECT_EQ(GetJsonString(ann, "name"), "Ann");
        EXPECT_EQ(GetJsonBool(ann, "connected"), true);
        const Json& bob = (*seats)[1];
        EXPECT_EQ(GetJsonBool(bob, "connected"), false);
        const Json& computer = (*seats)[2];
        EXPECT_EQ(GetJsonString(computer, "kind"), "computer");
        EXPECT_EQ(FindJsonMember(computer, "name"), nullptr);
        EXPECT_EQ(FindJsonMember(computer, "connected"), nullptr);
        const Json& empty = (*seats)[3];
        EXPECT_EQ(GetJsonString(empty, "kind"), "empty");
        EXPECT_EQ(GetJsonInt(empty, "seat"), 3);
        EXPECT_EQ(FindJsonMember(empty, "name"), nullptr);
    }

    TEST(RoomProtocolTest, LobbyPhasesAndNoHost) {
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = "fake";
        lobby.hostSeat = -1;
        for (const auto& [phase, name] : {std::pair{RoomPhase::Lobby, "lobby"}, std::pair{RoomPhase::Playing, "playing"}, std::pair{RoomPhase::Finished, "finished"}}) {
            lobby.phase = phase;
            const Json message = ParseOrNull(MakeLobbyMessage(lobby));
            EXPECT_EQ(GetJsonString(message, "phase"), eastl::string(name));
            EXPECT_EQ(GetJsonInt(message, "host"), -1);
        }
    }

    TEST(RoomProtocolTest, LobbyWithAnInvalidUtf8NameStillParses) {
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = "fake";
        lobby.seats.push_back(LobbySeat{0, SeatKind::Human, "bad\xC3\x28name", true});
        const eastl::string text = MakeLobbyMessage(lobby);
        const auto parsed = ParseJson(text);
        ASSERT_TRUE(parsed.has_value()) << text.c_str();
        const Json* seats = FindJsonMember(*parsed, "seats");
        ASSERT_NE(seats, nullptr);
        ASSERT_EQ(seats->size(), 1u);
        EXPECT_TRUE(GetJsonString((*seats)[0], "name").has_value());
    }

    TEST(RoomProtocolTest, ViewMessage) {
        Json view = Json::object();
        view["turn"] = 1;
        Json actions = Json::array();
        Json play = Json::object();
        play["type"] = "play";
        actions.push_back(play);

        const Json message = ParseOrNull(MakeViewMessage("ABCDE", 1, 42, false, view, actions));
        EXPECT_EQ(GetJsonString(message, "type"), "view");
        EXPECT_EQ(GetJsonString(message, "room"), "ABCDE");
        EXPECT_EQ(GetJsonInt(message, "seat"), 1);
        EXPECT_EQ(GetJsonInt(message, "version"), 42);
        EXPECT_EQ(GetJsonBool(message, "finished"), false);
        const Json* viewMember = FindJsonMember(message, "view");
        ASSERT_NE(viewMember, nullptr);
        EXPECT_EQ(GetJsonInt(*viewMember, "turn"), 1);
        const Json* actionsMember = FindJsonMember(message, "actions");
        ASSERT_NE(actionsMember, nullptr);
        ASSERT_TRUE(actionsMember->is_array());
        EXPECT_EQ(actionsMember->size(), 1u);
    }

    TEST(RoomProtocolTest, ErrorMessage) {
        const Json plain = ParseOrNull(MakeErrorMessage("bad_message", "nope"));
        EXPECT_EQ(GetJsonString(plain, "type"), "error");
        EXPECT_EQ(GetJsonString(plain, "code"), "bad_message");
        EXPECT_EQ(GetJsonString(plain, "message"), "nope");
        EXPECT_EQ(FindJsonMember(plain, "ref"), nullptr);

        const Json withRef = ParseOrNull(MakeErrorMessage("not_host", "only the host starts", "start"));
        EXPECT_EQ(GetJsonString(withRef, "ref"), "start");
    }

    TEST(RoomProtocolTest, LeftAndPong) {
        const Json left = ParseOrNull(MakeLeftMessage("replaced"));
        EXPECT_EQ(GetJsonString(left, "type"), "left");
        EXPECT_EQ(GetJsonString(left, "reason"), "replaced");
        EXPECT_EQ(GetJsonInt(left, "v"), 1);

        const Json pong = ParseOrNull(MakePongMessage());
        EXPECT_EQ(GetJsonString(pong, "type"), "pong");
        EXPECT_EQ(GetJsonInt(pong, "v"), 1);
    }

    TEST(RoomProtocolTest, EveryServerMessageCarriesTheVersionAndAType) {
        const eastl::vector<eastl::string> messages = {
            MakeJoinedMessage("ABCDE", 1, "t", 0, false),
            MakeLobbyMessage(LobbySnapshot{}),
            MakeViewMessage("ABCDE", 0, 0, false, Json::object(), Json::array()),
            MakeErrorMessage("x", "y"),
            MakeLeftMessage("z"),
            MakePongMessage(),
        };
        for (const eastl::string& text : messages) {
            const Json message = ParseOrNull(text);
            EXPECT_EQ(GetJsonInt(message, "v"), kRoomProtocolVersion) << text.c_str();
            EXPECT_TRUE(GetJsonString(message, "type").has_value()) << text.c_str();
        }
    }

    TEST(RoomProtocolTest, ServerMessageStaysInsideTheLimitForAFullLobby) {
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = Repeat('g', RoomLimits::kMaxGameNameBytes);
        for (int seat = 0; seat < 8; ++seat) {
            lobby.seats.push_back(LobbySeat{seat, SeatKind::Human, Repeat('n', RoomLimits::kMaxNameBytes), true});
        }
        EXPECT_LT(MakeLobbyMessage(lobby).size(), RoomLimits::kMaxMessageBytes);
    }

}  // namespace BECore::Tests
