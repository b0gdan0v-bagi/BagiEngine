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
        // Nesting is bounded whatever member it is in (see TheNestingDepthIsBounded): `options` and `action` are copied, and that recurses
        EXPECT_EQ(ErrorCodeOfText(bombInsideAMember), "bad_message");
    }

    TEST(RoomProtocolTest, TheNestingDepthIsBounded) {
        // A message of 16 KiB can nest 8000 levels; copying `options` or `action` (the hub keeps the first until the start) recurses once per level on the reader thread
        const auto deep = [](eastl::string_view prefix, size_t levels) {
            eastl::string text(prefix.data(), prefix.size());
            text.append(levels, '[');
            text.append(levels, ']');
            text += "}}";
            return text;
        };
        // The message object and the member are two levels of their own
        constexpr size_t kOwnLevels = 2;
        const eastl::string createPrefix = R"({"v":1,"type":"create_room","game":"demo","name":"Ann","options":{"a":)";
        const eastl::string actionPrefix = R"({"v":1,"type":"action","version":1,"action":{"a":)";
        for (const eastl::string& prefix : {createPrefix, actionPrefix}) {
            EXPECT_EQ(ErrorCodeOfText(deep(prefix, RoomLimits::kMaxClientJsonDepth - kOwnLevels)), "<ok>") << prefix.c_str();
            EXPECT_EQ(ErrorCodeOfText(deep(prefix, RoomLimits::kMaxClientJsonDepth - kOwnLevels + 1)), "bad_message") << prefix.c_str();
            EXPECT_EQ(ErrorCodeOfText(deep(prefix, 7000)), "bad_message") << prefix.c_str();
        }
        // Brackets inside strings are not levels
        EXPECT_EQ(ErrorCodeOfText(Message("create_room", Format(R"("game":"demo","name":"Ann","options":{{"a":"{}"}})", Repeat('[', 1000)))), "<ok>");
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

    // ---- Server messages read by a client -----------------------------------------------------------------------------------------

    namespace {

        constexpr eastl::string_view kToken = "00112233445566778899aabbccddeeff";

        template <typename T>
        const T* ServerAs(const std::expected<ServerMessage, ControlError>& parsed) {
            if (!parsed) {
                return nullptr;
            }
            return std::get_if<T>(&*parsed);
        }

        eastl::string ServerErrorCodeOf(eastl::string_view text, size_t maxBytes = RoomLimits::kMaxServerMessageBytes) {
            const auto parsed = ParseServerMessage(text, maxBytes);
            return parsed ? eastl::string("<ok>") : parsed.error().code;
        }

        /// A `lobby` message with @p seats as the JSON text of the array
        eastl::string LobbyWith(eastl::string_view members) {
            return Message("lobby", members);
        }

        constexpr eastl::string_view kGoodLobbyMembers = R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[])";

    }  // namespace

    TEST(RoomProtocolTest, ParseServerMessageReadsJoined) {
        const auto parsed = ParseServerMessage(MakeJoinedMessage("ABCDE", 3, kToken, 2, true));
        const auto* joined = ServerAs<JoinedMessage>(parsed);
        ASSERT_NE(joined, nullptr);
        EXPECT_EQ(joined->room, "ABCDE");
        EXPECT_EQ(joined->player, 3u);
        EXPECT_EQ(joined->token, kToken);
        EXPECT_EQ(joined->seat, 2);
        EXPECT_TRUE(joined->host);
        EXPECT_EQ(GetServerMessageType(*parsed), "joined");
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsALobbyWithAllThreeSeatKinds) {
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = "fake";
        lobby.phase = RoomPhase::Playing;
        lobby.hostSeat = 1;
        lobby.seats.push_back(LobbySeat{0, SeatKind::Computer, {}, false});
        lobby.seats.push_back(LobbySeat{1, SeatKind::Human, "Ann", true});
        lobby.seats.push_back(LobbySeat{2, SeatKind::Human, "Bob", false});
        lobby.seats.push_back(LobbySeat{3, SeatKind::Empty, {}, false});

        const auto parsed = ParseServerMessage(MakeLobbyMessage(lobby));
        const auto* read = ServerAs<LobbyMessage>(parsed);
        ASSERT_NE(read, nullptr);
        EXPECT_EQ(read->room, "ABCDE");
        EXPECT_EQ(read->game, "fake");
        EXPECT_EQ(read->phase, RoomPhase::Playing);
        EXPECT_EQ(read->hostSeat, 1);
        ASSERT_EQ(read->seats.size(), 4u);
        EXPECT_EQ(read->seats[0].kind, SeatKind::Computer);
        EXPECT_EQ(read->seats[1].kind, SeatKind::Human);
        EXPECT_EQ(read->seats[1].name, "Ann");
        EXPECT_TRUE(read->seats[1].connected);
        EXPECT_EQ(read->seats[2].name, "Bob");
        EXPECT_FALSE(read->seats[2].connected);
        EXPECT_EQ(read->seats[3].kind, SeatKind::Empty);
        EXPECT_EQ(read->seats[3].seat, 3);
        // Built again from what was read, it is the same text
        EXPECT_EQ(MakeLobbyMessage(*read), MakeLobbyMessage(lobby));
        EXPECT_EQ(GetServerMessageType(*parsed), "lobby");
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsEveryPhaseAndNoHost) {
        for (const RoomPhase phase : {RoomPhase::Lobby, RoomPhase::Playing, RoomPhase::Finished}) {
            LobbySnapshot lobby;
            lobby.room = "ABCDE";
            lobby.game = "fake";
            lobby.phase = phase;
            lobby.hostSeat = -1;
            const auto parsed = ParseServerMessage(MakeLobbyMessage(lobby));
            const auto* read = ServerAs<LobbyMessage>(parsed);
            ASSERT_NE(read, nullptr);
            EXPECT_EQ(read->phase, phase);
            EXPECT_EQ(read->hostSeat, -1);
        }
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsAViewWithNestedViewAndActions) {
        Json view = Json::object();
        view["hand"] = Json::array({1, 2, 3});
        view["table"] = Json::object({{"top", Json::object({{"card", 7}})}});
        Json play = Json::object();
        play["type"] = "play";
        play["card"] = 2;
        const Json actions = Json::array({play});

        const auto parsed = ParseServerMessage(MakeViewMessage("ABCDE", 1, 42, true, view, actions));
        const auto* read = ServerAs<ViewMessage>(parsed);
        ASSERT_NE(read, nullptr);
        EXPECT_EQ(read->room, "ABCDE");
        EXPECT_EQ(read->seat, 1);
        EXPECT_EQ(read->version, 42u);
        EXPECT_TRUE(read->finished);
        EXPECT_EQ(read->view, view);
        EXPECT_EQ(read->actions, actions);
        EXPECT_EQ(GetServerMessageType(*parsed), "view");
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsErrorsWithAndWithoutRef) {
        const auto plain = ParseServerMessage(MakeErrorMessage("bad_message", "nope"));
        const auto* plainError = ServerAs<ErrorMessage>(plain);
        ASSERT_NE(plainError, nullptr);
        EXPECT_EQ(plainError->code, "bad_message");
        EXPECT_EQ(plainError->message, "nope");
        EXPECT_TRUE(plainError->ref.empty());

        const auto withRef = ParseServerMessage(MakeErrorMessage("not_host", "only the host starts", "start"));
        const auto* refError = ServerAs<ErrorMessage>(withRef);
        ASSERT_NE(refError, nullptr);
        EXPECT_EQ(refError->ref, "start");
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsLeftAndPong) {
        const auto left = ParseServerMessage(MakeLeftMessage("replaced"));
        const auto* leftMessage = ServerAs<LeftMessage>(left);
        ASSERT_NE(leftMessage, nullptr);
        EXPECT_EQ(leftMessage->reason, "replaced");

        const auto pong = ParseServerMessage(MakePongMessage());
        EXPECT_NE(ServerAs<PongMessage>(pong), nullptr);
        EXPECT_EQ(GetServerMessageType(*pong), "pong");
    }

    TEST(RoomProtocolTest, ParseServerMessageIgnoresUnknownMembersAndTypes) {
        const auto joined = ParseServerMessage(Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":0,"host":false,"extra":[1,{{}}])", kToken)));
        EXPECT_NE(ServerAs<JoinedMessage>(joined), nullptr);

        const auto unknown = ParseServerMessage(Message("emote", R"("who":3)"));
        const auto* unknownMessage = ServerAs<UnknownServerMessage>(unknown);
        ASSERT_NE(unknownMessage, nullptr);
        EXPECT_EQ(unknownMessage->type, "emote");
        EXPECT_EQ(GetServerMessageType(*unknown), "emote");

        // A client's type from the server is only unknown to a client
        EXPECT_NE(ServerAs<UnknownServerMessage>(ParseServerMessage(kPlainPing)), nullptr);
    }

    TEST(RoomProtocolTest, ParseServerMessageRefusesWhatIsNotAWellFormedMessage) {
        const eastl::string goodJoined = Format(R"("room":"ABCDE","player":1,"token":"{}","seat":0,"host":false)", kToken);
        const eastl::string goodView = R"("room":"ABCDE","seat":0,"version":1,"finished":false,"view":{},"actions":[])";
        const eastl::string bomb = Repeat('[', RoomLimits::kMaxServerMessageBytes);
        eastl::string balancedBomb = Repeat('[', RoomLimits::kMaxServerMessageBytes / 2);
        balancedBomb.append(RoomLimits::kMaxServerMessageBytes / 2, ']');
        const eastl::string nulByte = eastl::string("{\"v\":1,\"type\":\"pong\"}\0garbage", 24);
        const eastl::string invalidUtf8 = Format("{{\"v\":1,\"type\":\"left\",\"reason\":\"{}\"}}", "\xC3\x28");
        // Valid JSON in the size limit, nested far deeper than a game's view is: the copy of "view" or "actions" would recurse
        const size_t bombDepth = (RoomLimits::kMaxServerMessageBytes - 256) / 2;
        const auto bombIn = [&](eastl::string_view member, size_t levels) {
            eastl::string open = Repeat('[', levels);
            open.append(levels, ']');
            const eastl::string others = member == "view" ? R"("actions":[])" : R"("view":{})";
            return Message("view", Format(R"("room":"ABCDE","seat":0,"version":1,"finished":false,{},"{}":{})", others, member, open));
        };
        const eastl::string bombInsideView = bombIn("view", bombDepth);
        const eastl::string bombInsideActions = bombIn("actions", bombDepth);
        const eastl::string justOverTheDepth = bombIn("view", RoomLimits::kMaxServerJsonDepth);

        const eastl::vector<BadInput> table = {
            {"empty", "", "bad_message"},
            {"not json", "hello", "bad_message"},
            {"truncated", R"({"v":1,"type":"pong")", "bad_message"},
            {"two values", R"({"v":1,"type":"pong"}{"v":1,"type":"pong"})", "bad_message"},
            {"nul after the object", nulByte, "bad_message"},
            {"invalid utf-8", invalidUtf8, "bad_message"},
            {"array", R"([{"v":1,"type":"pong"}])", "bad_message"},
            {"null", "null", "bad_message"},
            {"nesting bomb", bomb, "bad_message"},
            {"nesting bomb, balanced", balancedBomb, "bad_message"},
            {"nesting bomb inside view", bombInsideView, "bad_message"},
            {"nesting bomb inside actions", bombInsideActions, "bad_message"},
            {"one level over the depth bound", justOverTheDepth, "bad_message"},
            {"v missing", R"({"type":"pong"})", "bad_message"},
            {"v string", R"({"v":"1","type":"pong"})", "bad_message"},
            {"v fractional", R"({"v":1.5,"type":"pong"})", "bad_message"},
            {"v huge", R"({"v":1e300,"type":"pong"})", "bad_message"},
            {"v beyond int64", R"({"v":18446744073709551616,"type":"pong"})", "bad_message"},
            {"v two", R"({"v":2,"type":"pong"})", "unsupported_version"},
            {"v zero", R"({"v":0,"type":"pong"})", "unsupported_version"},
            {"type missing", R"({"v":1})", "bad_message"},
            {"type number", R"({"v":1,"type":5})", "bad_message"},
            {"type null", R"({"v":1,"type":null})", "bad_message"},
            {"joined: room missing", Message("joined", Format(R"("player":1,"token":"{}","seat":0,"host":false)", kToken)), "bad_message"},
            {"joined: room number", Message("joined", Format(R"("room":5,"player":1,"token":"{}","seat":0,"host":false)", kToken)), "bad_message"},
            {"joined: player fractional", Message("joined", Format(R"("room":"ABCDE","player":1.5,"token":"{}","seat":0,"host":false)", kToken)), "bad_message"},
            {"joined: player negative", Message("joined", Format(R"("room":"ABCDE","player":-1,"token":"{}","seat":0,"host":false)", kToken)), "bad_message"},
            {"joined: player over 32 bits", Message("joined", Format(R"("room":"ABCDE","player":4294967296,"token":"{}","seat":0,"host":false)", kToken)), "bad_message"},
            {"joined: token missing", Message("joined", R"("room":"ABCDE","player":1,"seat":0,"host":false)"), "bad_message"},
            {"joined: token short", Message("joined", R"("room":"ABCDE","player":1,"token":"0123","seat":0,"host":false)"), "bad_message"},
            {"joined: token upper case", Message("joined", R"("room":"ABCDE","player":1,"token":"0123456789ABCDEF0123456789ABCDEF","seat":0,"host":false)"), "bad_message"},
            {"joined: seat negative", Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":-1,"host":false)", kToken)), "bad_message"},
            {"joined: seat huge", Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":1e30,"host":false)", kToken)), "bad_message"},
            {"joined: seat over int", Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":2147483648,"host":false)", kToken)), "bad_message"},
            {"joined: host missing", Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":0)", kToken)), "bad_message"},
            {"joined: host number", Message("joined", Format(R"("room":"ABCDE","player":1,"token":"{}","seat":0,"host":1)", kToken)), "bad_message"},
            {"lobby: room missing", LobbyWith(R"("game":"g","phase":"lobby","host":0,"seats":[])"), "bad_message"},
            {"lobby: game missing", LobbyWith(R"("room":"ABCDE","phase":"lobby","host":0,"seats":[])"), "bad_message"},
            {"lobby: phase missing", LobbyWith(R"("room":"ABCDE","game":"g","host":0,"seats":[])"), "bad_message"},
            {"lobby: phase unknown", LobbyWith(R"("room":"ABCDE","game":"g","phase":"paused","host":0,"seats":[])"), "bad_message"},
            {"lobby: phase in other case", LobbyWith(R"("room":"ABCDE","game":"g","phase":"Lobby","host":0,"seats":[])"), "bad_message"},
            {"lobby: phase number", LobbyWith(R"("room":"ABCDE","game":"g","phase":1,"host":0,"seats":[])"), "bad_message"},
            {"lobby: host below -1", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":-2,"seats":[])"), "bad_message"},
            {"lobby: host fractional", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0.5,"seats":[])"), "bad_message"},
            {"lobby: seats missing", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0)"), "bad_message"},
            {"lobby: seats object", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":{})"), "bad_message"},
            {"lobby: seat is a number", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[1])"), "bad_message"},
            {"lobby: seat kind unknown", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0,"kind":"robot"}])"), "bad_message"},
            {"lobby: seat kind missing", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0}])"), "bad_message"},
            {"lobby: seat index fractional", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0.5,"kind":"empty"}])"), "bad_message"},
            {"lobby: seat index negative", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":-1,"kind":"empty"}])"), "bad_message"},
            {"lobby: human without a name", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0,"kind":"human","connected":true}])"), "bad_message"},
            {"lobby: human without connected", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0,"kind":"human","name":"Ann"}])"), "bad_message"},
            {"lobby: human name number", LobbyWith(R"("room":"ABCDE","game":"g","phase":"lobby","host":0,"seats":[{"seat":0,"kind":"human","name":7,"connected":true}])"), "bad_message"},
            {"view: room missing", Message("view", R"("seat":0,"version":1,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: seat string", Message("view", R"("room":"ABCDE","seat":"0","version":1,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: version missing", Message("view", R"("room":"ABCDE","seat":0,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: version negative", Message("view", R"("room":"ABCDE","seat":0,"version":-1,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: version fractional", Message("view", R"("room":"ABCDE","seat":0,"version":1.5,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: version beyond int64", Message("view", R"("room":"ABCDE","seat":0,"version":9223372036854775808,"finished":false,"view":{},"actions":[])"), "bad_message"},
            {"view: finished string", Message("view", R"("room":"ABCDE","seat":0,"version":1,"finished":"no","view":{},"actions":[])"), "bad_message"},
            {"view: view missing", Message("view", R"("room":"ABCDE","seat":0,"version":1,"finished":false,"actions":[])"), "bad_message"},
            {"view: actions missing", Message("view", R"("room":"ABCDE","seat":0,"version":1,"finished":false,"view":{})"), "bad_message"},
            {"error: code missing", Message("error", R"("message":"x")"), "bad_message"},
            {"error: code number", Message("error", R"("code":5,"message":"x")"), "bad_message"},
            {"error: message missing", Message("error", R"("code":"x")"), "bad_message"},
            {"error: ref number", Message("error", R"("code":"x","message":"y","ref":3)"), "bad_message"},
            {"left: reason missing", Message("left"), "bad_message"},
            {"left: reason number", Message("left", R"("reason":1)"), "bad_message"},
        };
        for (const BadInput& input : table) {
            EXPECT_EQ(ServerErrorCodeOf(input.text), eastl::string(input.code)) << input.label;
        }
        // The same members, well formed, pass
        EXPECT_EQ(ServerErrorCodeOf(Message("joined", goodJoined)), "<ok>");
        EXPECT_EQ(ServerErrorCodeOf(Message("view", goodView)), "<ok>");
        EXPECT_EQ(ServerErrorCodeOf(LobbyWith(kGoodLobbyMembers)), "<ok>");
        // The bound counts the message object too: a view that nests to exactly the bound is read
        EXPECT_EQ(ServerErrorCodeOf(bombIn("view", RoomLimits::kMaxServerJsonDepth - 1)), "<ok>");
        EXPECT_EQ(ServerErrorCodeOf(bombIn("actions", RoomLimits::kMaxServerJsonDepth - 1)), "<ok>");
    }

    TEST(RoomProtocolTest, ParseServerMessageBoundsTheSeatsOfALobby) {
        eastl::string seats = "[";
        for (size_t seat = 0; seat <= RoomLimits::kMaxLobbySeats; ++seat) {
            seats.append(seat == 0 ? "" : ",");
            seats.append(Format(R"({{"seat":{},"kind":"empty"}})", seat));
        }
        seats.append("]");
        EXPECT_EQ(ServerErrorCodeOf(LobbyWith(Format(R"("room":"ABCDE","game":"g","phase":"lobby","host":-1,"seats":{})", seats))), "bad_message");
    }

    TEST(RoomProtocolTest, ParseServerMessageChecksTheSizeFirst) {
        const eastl::string pong = MakePongMessage();
        EXPECT_EQ(ServerErrorCodeOf(pong, pong.size()), "<ok>");
        EXPECT_EQ(ServerErrorCodeOf(pong, pong.size() - 1), RoomErrors::kMessageTooLarge);
        EXPECT_EQ(ServerErrorCodeOf(Repeat('x', RoomLimits::kMaxServerMessageBytes + 1)), RoomErrors::kMessageTooLarge);
        // A view of almost a megabyte is read, a byte more is not
        eastl::string big = Message("pong", Format(R"("pad":"{}")", ""));
        big.erase(big.size() - 1);
        big.append(R"(,"pad":")");
        const size_t padding = RoomLimits::kMaxServerMessageBytes - big.size() - 2;
        big.append(padding, 'x');
        big.append("\"}");
        ASSERT_EQ(big.size(), RoomLimits::kMaxServerMessageBytes);
        EXPECT_EQ(ServerErrorCodeOf(big), "<ok>");
        big.insert(big.size() - 2, "x");
        EXPECT_EQ(ServerErrorCodeOf(big), RoomErrors::kMessageTooLarge);
    }

    TEST(RoomProtocolTest, ParseServerMessageNeverAbortsOnAnyByte) {
        for (int byte = 0; byte < 256; ++byte) {
            const eastl::string one(1, static_cast<char>(byte));
            EXPECT_FALSE(ParseServerMessage(one).has_value()) << byte;
            const eastl::string inside = Format("{{\"v\":1,\"type\":\"error\",\"code\":\"{}\",\"message\":\"{}\"}}", one, one);
            const auto parsed = ParseServerMessage(inside);
            EXPECT_TRUE(parsed.has_value() || !parsed.error().code.empty()) << byte;
        }
        SUCCEED();
    }

    TEST(RoomProtocolTest, ParseServerMessageReadsWhatTheServerBuildsForAFullLobbyAndAGameSizedView) {
        // The biggest honest lobby, and a view of a few hundred KiB: both come back whole
        LobbySnapshot lobby;
        lobby.room = "ABCDE";
        lobby.game = Repeat('g', RoomLimits::kMaxGameNameBytes);
        for (int seat = 0; seat < 8; ++seat) {
            lobby.seats.push_back(LobbySeat{seat, SeatKind::Human, Repeat('n', RoomLimits::kMaxNameBytes), true});
        }
        const auto parsedLobby = ParseServerMessage(MakeLobbyMessage(lobby));
        ASSERT_NE(ServerAs<LobbyMessage>(parsedLobby), nullptr);

        Json hand = Json::array();
        for (int i = 0; i < 20000; ++i) {
            hand.push_back(i);
        }
        Json view = Json::object();
        view["hand"] = hand;
        const eastl::string text = MakeViewMessage("ABCDE", 0, 5, false, view, Json::array());
        ASSERT_GT(text.size(), RoomLimits::kMaxMessageBytes);
        ASSERT_LT(text.size(), RoomLimits::kMaxServerMessageBytes);
        const auto parsedView = ParseServerMessage(text);
        const auto* read = ServerAs<ViewMessage>(parsedView);
        ASSERT_NE(read, nullptr);
        EXPECT_EQ(read->view, view);
    }

}  // namespace BECore::Tests
