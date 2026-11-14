#include "TestClient.h"

namespace BECore::Tests {

    /// `create_room {public}` and `list_rooms`: what the hub lists, to whom, and how often. The room-level part (a flag that is no lobby change) is at the end.

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        /// The entries of the last `rooms` message the client got (empty when none)
        const Json* RoomsOf(const TestClient& client) {
            const ReceivedMessage* message = client.Last(RoomMessageTypes::kRooms);
            return message ? FindJsonMember(message->json, "rooms") : nullptr;
        }

        size_t ListedCount(const TestClient& client) {
            const Json* rooms = RoomsOf(client);
            return rooms ? rooms->size() : 0;
        }

        String CodeAt(const TestClient& client, size_t index) {
            const Json* rooms = RoomsOf(client);
            if (rooms == nullptr || index >= rooms->size()) {
                return {};
            }
            return GetJsonString((*rooms)[index], "room").value_or(String());
        }

        bool IsListed(const TestClient& client, const String& code) {
            for (size_t i = 0; i < ListedCount(client); ++i) {
                if (CodeAt(client, i) == code) {
                    return true;
                }
            }
            return false;
        }

        /// A host makes a public room of @p game; returns the host's connection (the code is in its client)
        ConnectionId MakePublicRoom(HubHarness& harness, StringView name, StringView game = "fake", Json options = Json::object()) {
            const ConnectionId host = harness.Connect();
            harness.Create(host, name, game, std::move(options), true);
            return host;
        }

        /// A browser: a connection in no room that lists @p game once; returns it
        ConnectionId Browse(HubHarness& harness, StringView game = "fake") {
            const ConnectionId browser = harness.Connect();
            harness.List(browser, game);
            return browser;
        }

        /// Whether any text any client received that is not in @p members contains @p code
        size_t CountTextsWithCode(HubHarness& harness, const String& code, const eastl::vector<ConnectionId>& members) {
            size_t count = 0;
            for (const auto& [id, client] : harness.Clients()) {
                if (eastl::find(members.begin(), members.end(), id) != members.end()) {
                    continue;
                }
                for (const ReceivedMessage& message : client.received) {
                    count += message.text.find(code) != String::npos ? 1 : 0;
                }
            }
            return count;
        }

        RoomHubSettings SettingsWith(size_t maxListed) {
            RoomHubSettings settings = HubHarness::MakeSettings();
            settings.maxListedRooms = maxListed;
            return settings;
        }

    }  // namespace

    TEST(RoomHubTest, APrivateRoomIsNeverListedAndItsCodeGoesToItsMembersOnly) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        harness.Create(host, "Ann");
        const String privateCode = harness.Client(host).roomCode;
        const ConnectionId guest = harness.Connect();
        harness.JoinByName(guest, privateCode, "Bob");
        const ConnectionId publicHost = MakePublicRoom(harness, "Cid");
        const String publicCode = harness.Client(publicHost).roomCode;

        const ConnectionId browser = Browse(harness);
        EXPECT_EQ(ListedCount(harness.Client(browser)), 1u);
        EXPECT_TRUE(IsListed(harness.Client(browser), publicCode));
        EXPECT_FALSE(IsListed(harness.Client(browser), privateCode));

        // Nobody outside the room ever got the code, whatever they did meanwhile: list again, ask for the other game, ping, guess codes
        harness.List(browser);
        harness.List(browser, "fake-public");
        harness.Ping(browser);
        harness.JoinByName(browser, "ZZZZZ", "Eve");
        EXPECT_EQ(CountTextsWithCode(harness, privateCode, {host, guest}), 0u);
        EXPECT_GT(CountTextsWithCode(harness, publicCode, {publicHost}), 0u);
        // The members did get it
        EXPECT_GT(CountTextsWithCode(harness, privateCode, {browser, publicHost}), 0u);
        EXPECT_EQ(harness.GetLeakCount(), 0u);
    }

    TEST(RoomHubTest, ACreateWithPublicFalseIsPrivateAndTheLobbyDoesNotSayWhetherARoomIsListed) {
        HubHarness harness;
        const ConnectionId host = harness.Connect();
        harness.Send(host, CreateRoomMessage{"fake", "Ann", Json::object(), false});
        const ConnectionId publicHost = MakePublicRoom(harness, "Bob");
        EXPECT_EQ(ListedCount(harness.Client(Browse(harness))), 1u);
        EXPECT_EQ(FindJsonMember(harness.Client(publicHost).Last(RoomMessageTypes::kLobby)->json, "public"), nullptr);
        EXPECT_EQ(FindJsonMember(harness.Client(publicHost).Last(RoomMessageTypes::kJoined)->json, "public"), nullptr);
    }

    TEST(RoomHubTest, APublicRoomIsListedWithTheHostTheSeatsAndTheOptions) {
        HubHarness harness;
        Json options = Json::object();
        options["open"] = true;
        options["count"] = 3;
        options["secret"] = 9;  // not in publicOptions
        const ConnectionId host = MakePublicRoom(harness, "Ann", "fake-public", options);
        const String code = harness.Client(host).roomCode;

        const ConnectionId browser = Browse(harness, "fake-public");
        TestClient& client = harness.Client(browser);
        EXPECT_EQ(client.Types(), "rooms");
        const ReceivedMessage* answer = client.Last(RoomMessageTypes::kRooms);
        ASSERT_NE(answer, nullptr);
        EXPECT_EQ(answer->GetString("game"), "fake-public");
        EXPECT_EQ(GetJsonBool(answer->json, "more"), false);
        ASSERT_EQ(ListedCount(client), 1u);
        const Json& entry = (*RoomsOf(client))[0];
        EXPECT_EQ(GetJsonString(entry, "room"), code);
        EXPECT_EQ(GetJsonString(entry, "host"), "Ann");
        EXPECT_EQ(GetJsonInt(entry, "taken"), 1);
        EXPECT_EQ(GetJsonInt(entry, "seats"), FakeSeatedGame::kSeatCount);
        EXPECT_EQ(GetJsonBool(entry, "started"), false);
        const Json* shown = FindJsonMember(entry, "options");
        ASSERT_NE(shown, nullptr);
        Json expected = Json::object();
        expected["open"] = true;
        expected["count"] = 3;
        EXPECT_EQ(*shown, expected);

        // A room without public options has no `options` member; the answer reads back with the client's reader
        const ConnectionId plainHost = MakePublicRoom(harness, "Bob");
        harness.List(browser);
        EXPECT_EQ(FindJsonMember((*RoomsOf(client))[0], "options"), nullptr);
        const auto parsed = ParseServerMessage(client.Last(RoomMessageTypes::kRooms)->text);
        ASSERT_TRUE(parsed.has_value());
        EXPECT_TRUE(std::holds_alternative<RoomListMessage>(*parsed));
        EXPECT_EQ(harness.Client(plainHost).roomCode, GetJsonString((*RoomsOf(client))[0], "room").value_or(String()));
    }

    TEST(RoomHubTest, ARoomDropsOutOfTheListWhenFullAndComesBackWhenASeatIsFree) {
        HubHarness harness;
        const ConnectionId host = MakePublicRoom(harness, "Ann");
        const String code = harness.Client(host).roomCode;
        eastl::vector<ConnectionId> guests;
        for (int i = 1; i < FakeSeatedGame::kSeatCount; ++i) {
            const ConnectionId guest = harness.Connect();
            harness.JoinByName(guest, code, Format("P{}", i).c_str());
            guests.push_back(guest);
            const ConnectionId browser = Browse(harness);
            EXPECT_EQ(IsListed(harness.Client(browser), code), i < FakeSeatedGame::kSeatCount - 1) << i;
            if (i < FakeSeatedGame::kSeatCount - 1) {
                const Json& entry = (*RoomsOf(harness.Client(browser)))[0];
                EXPECT_EQ(GetJsonInt(entry, "taken"), i + 1);
            }
        }
        harness.Leave(guests.back());
        EXPECT_TRUE(IsListed(harness.Client(Browse(harness)), code));
    }

    TEST(RoomHubTest, AStartedRoomIsNotListedUnlessTheGameSaysSoAndAFinishedOneNever) {
        HubHarness harness;
        const ConnectionId plain = MakePublicRoom(harness, "Ann", "fake");
        const ConnectionId listing = MakePublicRoom(harness, "Bob", "fake-started");
        const String plainCode = harness.Client(plain).roomCode;
        const String listingCode = harness.Client(listing).roomCode;
        EXPECT_TRUE(IsListed(harness.Client(Browse(harness, "fake")), plainCode));
        EXPECT_TRUE(IsListed(harness.Client(Browse(harness, "fake-started")), listingCode));

        harness.Start(plain);
        harness.Start(listing);
        EXPECT_FALSE(IsListed(harness.Client(Browse(harness, "fake")), plainCode));
        const ConnectionId browser = Browse(harness, "fake-started");
        ASSERT_TRUE(IsListed(harness.Client(browser), listingCode));
        const Json& entry = (*RoomsOf(harness.Client(browser)))[0];
        EXPECT_EQ(GetJsonBool(entry, "started"), true);
        EXPECT_EQ(GetJsonInt(entry, "taken"), 1);
        EXPECT_EQ(GetJsonString(entry, "host"), "Bob");

        // Nobody can join a started room, from a list or not
        const ConnectionId late = harness.Connect();
        harness.JoinByName(late, listingCode, "Late");
        EXPECT_EQ(harness.Client(late).LastErrorCode(), RoomErrors::kGameStarted);

        // Played to the end the room is no longer listed, even for the game that lists started rooms
        for (int step = 0; step < 400 && !GetJsonBool(harness.Client(listing).Last(RoomMessageTypes::kView)->json, "finished").value_or(false); ++step) {
            harness.Tick(milliseconds(10));
            harness.PlayFirstAction(listing);
        }
        ASSERT_TRUE(GetJsonBool(harness.Client(listing).Last(RoomMessageTypes::kView)->json, "finished").value_or(false));
        EXPECT_FALSE(IsListed(harness.Client(Browse(harness, "fake-started")), listingCode));
    }

    TEST(RoomHubTest, AClosedRoomIsNotListedAndWhenTheHostLeavesTheNewHostIsShown) {
        HubHarness harness;
        const ConnectionId host = MakePublicRoom(harness, "Ann");
        const String code = harness.Client(host).roomCode;
        const ConnectionId guest = harness.Connect();
        harness.JoinByName(guest, code, "Bob");
        EXPECT_EQ(GetJsonString((*RoomsOf(harness.Client(Browse(harness))))[0], "host"), "Ann");

        harness.Leave(host);
        const ConnectionId second = Browse(harness);
        ASSERT_EQ(ListedCount(harness.Client(second)), 1u);
        EXPECT_EQ(GetJsonString((*RoomsOf(harness.Client(second)))[0], "host"), "Bob");
        EXPECT_EQ(GetJsonInt((*RoomsOf(harness.Client(second)))[0], "taken"), 1);

        harness.Leave(guest);
        EXPECT_FALSE(harness.Hub().HasRoom(code));
        const ConnectionId third = Browse(harness);
        EXPECT_EQ(ListedCount(harness.Client(third)), 0u);
        EXPECT_EQ(harness.Client(third).Types(), "rooms");
        EXPECT_EQ(GetJsonBool(harness.Client(third).Last(RoomMessageTypes::kRooms)->json, "more"), false);
    }

    TEST(RoomHubTest, ListingAsksForOneGameOnly) {
        HubHarness harness;
        const ConnectionId one = MakePublicRoom(harness, "Ann", "fake");
        const ConnectionId two = MakePublicRoom(harness, "Bob", "fake-public");
        const ConnectionId plainBrowser = Browse(harness, "fake");
        EXPECT_EQ(ListedCount(harness.Client(plainBrowser)), 1u);
        EXPECT_EQ(CodeAt(harness.Client(plainBrowser), 0), harness.Client(one).roomCode);
        const ConnectionId publicBrowser = Browse(harness, "fake-public");
        EXPECT_EQ(ListedCount(harness.Client(publicBrowser)), 1u);
        EXPECT_EQ(CodeAt(harness.Client(publicBrowser), 0), harness.Client(two).roomCode);
    }

    TEST(RoomHubTest, ListingAnUnknownGameOrFromInsideARoomIsAnError) {
        HubHarness harness;
        const ConnectionId host = MakePublicRoom(harness, "Ann");
        const ConnectionId browser = harness.Connect();
        harness.List(browser, "no-such-game");
        EXPECT_EQ(harness.Client(browser).LastErrorCode(), RoomErrors::kUnknownGame);
        EXPECT_EQ(harness.Client(browser).Last(RoomMessageTypes::kError)->GetString("ref"), "list_rooms");
        EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), 0u);

        harness.Client(host).Clear();
        harness.List(host);
        EXPECT_EQ(harness.Client(host).LastErrorCode(), RoomErrors::kAlreadyInRoom);
        EXPECT_EQ(harness.Client(host).Last(RoomMessageTypes::kError)->GetString("ref"), "list_rooms");
        EXPECT_EQ(harness.Client(host).Types(), "error");
        // The members of a room are not told anything by a list that was refused
        EXPECT_FALSE(harness.Client(host).closed);
    }

    TEST(RoomHubTest, TheAnswerIsCutAtTheCapOldestFirst) {
        HubHarness harness(SettingsWith(2));
        eastl::vector<String> codes;
        for (int i = 0; i < 3; ++i) {
            codes.push_back(harness.Client(MakePublicRoom(harness, Format("H{}", i).c_str())).roomCode);
        }
        const ConnectionId browser = Browse(harness);
        TestClient& client = harness.Client(browser);
        ASSERT_EQ(ListedCount(client), 2u);
        EXPECT_EQ(CodeAt(client, 0), codes[0]);
        EXPECT_EQ(CodeAt(client, 1), codes[1]);
        EXPECT_EQ(GetJsonBool(client.Last(RoomMessageTypes::kRooms)->json, "more"), true);

        // Exactly at the cap is not "more"
        HubHarness exact(SettingsWith(3));
        for (int i = 0; i < 3; ++i) {
            MakePublicRoom(exact, Format("H{}", i).c_str());
        }
        const ConnectionId other = Browse(exact);
        EXPECT_EQ(ListedCount(exact.Client(other)), 3u);
        EXPECT_EQ(GetJsonBool(exact.Client(other).Last(RoomMessageTypes::kRooms)->json, "more"), false);
    }

    TEST(RoomHubTest, TheCapOfTheSettingsNeverGoesAboveTheLimitOfTheProtocol) {
        HubHarness harness(SettingsWith(1000));
        for (size_t i = 0; i < RoomLimits::kMaxListedRooms + 1; ++i) {
            MakePublicRoom(harness, "H");
        }
        const ConnectionId browser = Browse(harness);
        EXPECT_EQ(ListedCount(harness.Client(browser)), RoomLimits::kMaxListedRooms);
        EXPECT_EQ(GetJsonBool(harness.Client(browser).Last(RoomMessageTypes::kRooms)->json, "more"), true);
        // What the hub sends is what a client accepts
        EXPECT_TRUE(ParseServerMessage(harness.Client(browser).Last(RoomMessageTypes::kRooms)->text).has_value());
    }

    TEST(RoomHubTest, TheOrderIsTheOrderOfCreationWhateverTheCodes) {
        HubHarness harness;
        eastl::vector<String> codes;
        for (int i = 0; i < 12; ++i) {
            codes.push_back(harness.Client(MakePublicRoom(harness, "H")).roomCode);
        }
        const ConnectionId browser = Browse(harness);
        ASSERT_EQ(ListedCount(harness.Client(browser)), codes.size());
        for (size_t i = 0; i < codes.size(); ++i) {
            EXPECT_EQ(CodeAt(harness.Client(browser), i), codes[i]) << i;
        }
        // Two answers in a row are the same
        const String first = harness.Client(browser).Last(RoomMessageTypes::kRooms)->text;
        harness.List(browser);
        EXPECT_EQ(harness.Client(browser).Last(RoomMessageTypes::kRooms)->text, first);
    }

    TEST(RoomHubTest, TheListBucketAnswersRateLimitedKeepsTheConnectionAndRefills) {
        HubHarness harness;
        MakePublicRoom(harness, "Ann");
        const ConnectionId browser = harness.Connect();
        for (int i = 0; i < RoomLimits::kListBurst; ++i) {
            harness.List(browser);
        }
        EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), static_cast<size_t>(RoomLimits::kListBurst));
        // Many more than the general bucket's five in a row: the list bucket's refusals do not count towards closing
        harness.Client(browser).Clear();
        for (int i = 0; i < 12; ++i) {
            harness.List(browser);
        }
        EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), 0u);
        ASSERT_EQ(harness.Client(browser).Count(RoomMessageTypes::kError), 12u);
        const ReceivedMessage* refusal = harness.Client(browser).Last(RoomMessageTypes::kError);
        EXPECT_EQ(refusal->GetString("code"), RoomErrors::kRateLimited);
        EXPECT_EQ(refusal->GetString("ref"), "list_rooms");
        EXPECT_FALSE(harness.Client(browser).closed);

        // One more list per refill
        harness.Advance(RoomLimits::kListRefill);
        harness.List(browser);
        EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), 1u);
        harness.List(browser);
        EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), 1u);

        // The connection still works: it joins the room it saw
        const String code = CodeAt(harness.Client(browser), 0);
        harness.JoinByName(browser, code, "Bob");
        EXPECT_EQ(harness.Client(browser).Last(RoomMessageTypes::kJoined)->GetString("room"), code);
    }

    TEST(RoomHubTest, TheListBucketBelongsToTheConnection) {
        HubHarness harness;
        const ConnectionId first = harness.Connect();
        const ConnectionId second = harness.Connect();
        for (int i = 0; i < RoomLimits::kListBurst + 2; ++i) {
            harness.List(first);
        }
        harness.List(second);
        EXPECT_EQ(harness.Client(second).Count(RoomMessageTypes::kRooms), 1u);
        EXPECT_EQ(harness.Client(second).Count(RoomMessageTypes::kError), 0u);
    }

    TEST(RoomHubTest, ARefusedListDoesNotCountAsAFailedJoin) {
        HubHarness harness;
        const ConnectionId client = harness.Connect();
        for (int i = 0; i < RoomLimits::kMaxFailedJoinsInRow - 1; ++i) {
            harness.JoinByName(client, "ZZZZZ", "Eve");
        }
        EXPECT_FALSE(harness.Client(client).closed);
        // Lists, answered and refused, in between
        for (int i = 0; i < RoomLimits::kListBurst + 3; ++i) {
            harness.List(client);
        }
        EXPECT_FALSE(harness.Client(client).closed);
        harness.Advance(seconds(1));
        // Exactly one more failed join closes it: the count was four, not more and not fewer
        harness.JoinByName(client, "ZZZZZ", "Eve");
        EXPECT_TRUE(harness.Client(client).closed);
        EXPECT_EQ(harness.Client(client).closeReason, "too_many_failed_joins");
    }

    TEST(RoomHubTest, AFloodOfListsEndsAtTheGeneralLimitWithAClose) {
        HubHarness harness;
        const ConnectionId client = harness.Connect();
        for (int i = 0; i < 100 && !harness.Client(client).closed; ++i) {
            harness.List(client);
        }
        EXPECT_TRUE(harness.Client(client).closed);
        EXPECT_EQ(harness.Client(client).closeReason, RoomErrors::kRateLimited);
        // The first answers were lists, then the list bucket's refusals, then the general bucket's (no `ref`)
        EXPECT_EQ(harness.Client(client).Count(RoomMessageTypes::kRooms), static_cast<size_t>(RoomLimits::kListBurst));
        EXPECT_EQ(harness.Hub().GetConnectionCount(), 0u);
    }

    TEST(RoomHubTest, ListsNeverSpendTheFailedJoinBudgets) {
        RoomHubSettings settings = HubHarness::MakeSettings();
        settings.addressJoinBurst = 2;
        settings.serverJoinBurst = 2;
        settings.addressJoinRefill = std::chrono::milliseconds(0);
        settings.serverJoinRefill = std::chrono::milliseconds(0);
        settings.maxFailedJoinsInRow = 2;
        settings.rateBurst = 1000;
        settings.listBurst = 1000;
        HubHarness harness(settings);
        const ConnectionId host = MakePublicRoom(harness, "Ann");
        const String code = harness.Client(host).roomCode;
        for (int round = 0; round < 5; ++round) {
            const ConnectionId browser = harness.Connect("10.0.0.1");
            for (int i = 0; i < 50; ++i) {
                harness.List(browser);
            }
            EXPECT_FALSE(harness.Client(browser).closed) << round;
            EXPECT_EQ(harness.Client(browser).Count(RoomMessageTypes::kRooms), 50u) << round;
        }
        // The budgets of the address and of the server are still full: a join works, then a wrong code spends one, and so on
        const ConnectionId guest = harness.Connect("10.0.0.1");
        harness.JoinByName(guest, code, "Bob");
        EXPECT_EQ(harness.Client(guest).Last(RoomMessageTypes::kJoined)->GetString("room"), code);
        const ConnectionId guesser = harness.Connect("10.0.0.1");
        harness.JoinByName(guesser, "ZZZZZ", "Eve");
        EXPECT_EQ(harness.Client(guesser).LastErrorCode(), RoomErrors::kUnknownRoom);
        harness.JoinByName(guesser, "YYYYY", "Eve");
        EXPECT_EQ(harness.Client(guesser).LastErrorCode(), RoomErrors::kUnknownRoom);
        // Two wrong codes used the whole budget: now it says so, and it is the failed joins that did it
        const ConnectionId late = harness.Connect("10.0.0.1");
        harness.JoinByName(late, code, "Cid");
        EXPECT_EQ(harness.Client(late).LastErrorCode(), RoomErrors::kRateLimited);
    }

    TEST(RoomHubTest, AListCountsAsActivityForTheIdleTimeoutAndAPingDoesNot) {
        HubHarness harness;
        const ConnectionId lister = harness.Connect();
        const ConnectionId pinger = harness.Connect();
        harness.Tick(seconds(20));
        harness.List(lister);
        harness.Ping(pinger);
        harness.Tick(seconds(20));
        EXPECT_FALSE(harness.Client(lister).closed);
        EXPECT_TRUE(harness.Client(pinger).closed);
        EXPECT_EQ(harness.Client(pinger).closeReason, "idle");
    }

    // ---- Join from the list is join by code ---------------------------------------------------------------------------------------

    namespace {

        /// What a join gives back to the connection, with the parts that are chance (the room, the token) taken out
        String JoinOutcome(HubHarness& harness, ConnectionId id, const String& code, StringView name) {
            harness.Client(id).Clear();
            harness.JoinByName(id, code, name);
            const TestClient& client = harness.Client(id);
            if (client.closed) {
                return Format("closed:{}", client.closeReason);
            }
            if (client.Last(RoomMessageTypes::kError) != nullptr) {
                return client.Last(RoomMessageTypes::kError)->text;
            }
            return client.Types();
        }

    }  // namespace

    TEST(RoomHubTest, JoiningFromTheListGivesTheSameAnswersAsJoiningByCode) {
        // Each case: a room is listed, something happens to it, then one connection joins with the code it read from the list and another with the
        // code the host told it; the two get the same text. (A code from the list is the code: the client sends the same `join_room`.)
        enum class Case { Open, Full, Started, BadName, Closed };
        for (const Case which : {Case::Open, Case::Full, Case::Started, Case::BadName, Case::Closed}) {
            HubHarness harness;
            const ConnectionId host = MakePublicRoom(harness, "Ann", "fake-started");
            const String told = harness.Client(host).roomCode;
            const ConnectionId browser = Browse(harness, "fake-started");
            const String listed = CodeAt(harness.Client(browser), 0);
            ASSERT_EQ(listed, told);
            const ConnectionId viaCode = harness.Connect();
            String name = "Bob";
            switch (which) {
                case Case::Open:
                    break;
                case Case::Full:
                    for (int i = 1; i < FakeSeatedGame::kSeatCount; ++i) {
                        harness.JoinByName(harness.Connect(), told, Format("F{}", i).c_str());
                    }
                    break;
                case Case::Started:
                    harness.Start(host);
                    break;
                case Case::BadName:
                    name = "";
                    break;
                case Case::Closed:
                    harness.Leave(host);
                    break;
            }
            const String fromList = JoinOutcome(harness, browser, listed, name);
            const String byCode = JoinOutcome(harness, viaCode, told, name);
            EXPECT_EQ(fromList, byCode) << static_cast<int>(which);
            switch (which) {
                case Case::Open:
                    EXPECT_EQ(fromList, "joined,lobby");
                    break;
                case Case::Full:
                    EXPECT_NE(fromList.find("room_full"), String::npos);
                    break;
                case Case::Started:
                    EXPECT_NE(fromList.find("game_started"), String::npos);
                    break;
                case Case::BadName:
                    EXPECT_NE(fromList.find("bad_name"), String::npos);
                    break;
                case Case::Closed:
                    EXPECT_NE(fromList.find("unknown_room"), String::npos);
                    break;
            }
        }
    }

    TEST(RoomHubTest, ACodeFromAStaleListIsAFailedJoinLikeAnyUnknownCode) {
        HubHarness harness;
        const ConnectionId host = MakePublicRoom(harness, "Ann");
        const ConnectionId browser = Browse(harness);
        const String stale = CodeAt(harness.Client(browser), 0);
        harness.Leave(host);
        ASSERT_FALSE(harness.Hub().HasRoom(stale));
        for (int i = 0; i < RoomLimits::kMaxFailedJoinsInRow; ++i) {
            EXPECT_FALSE(harness.Client(browser).closed) << i;
            harness.JoinByName(browser, stale, "Bob");
            EXPECT_EQ(harness.Client(browser).LastErrorCode(), RoomErrors::kUnknownRoom);
        }
        EXPECT_TRUE(harness.Client(browser).closed);
        EXPECT_EQ(harness.Client(browser).closeReason, "too_many_failed_joins");
    }

    // ---- The room's part ----------------------------------------------------------------------------------------------------------

    TEST(RoomLobbyTest, ARoomIsPrivateUntilItIsMadePublicAndTheFlagIsNoLobbyChange) {
        Room room("ABCDE", SeatedGameInfo{"fake", 4}, MakeSequence(7));
        EXPECT_FALSE(room.IsPublic());
        const uint64_t revision = room.GetLobbyRevision();
        room.SetPublic(true);
        EXPECT_TRUE(room.IsPublic());
        EXPECT_EQ(room.GetLobbyRevision(), revision);
        room.SetPublic(false);
        EXPECT_FALSE(room.IsPublic());
    }

    TEST(RoomLobbyTest, TheListingNamesTheHostCountsTheHumansAndKeepsTheLobbyOptions) {
        Room room("ABCDE", SeatedGameInfo{"fake", 4, {"hints"}}, MakeSequence(7));
        Json options = Json::object();
        options["hints"] = true;
        options["other"] = 1;
        room.SetLobbyOptions(options);
        EXPECT_TRUE(room.GetListing().host.empty());
        EXPECT_EQ(room.GetListing().taken, 0);

        const auto first = room.AddMember("Ann", RoomTime{});
        const auto second = room.AddMember("Bob", RoomTime{});
        ASSERT_TRUE(first.has_value() && second.has_value());
        RoomListing listing = room.GetListing();
        EXPECT_EQ(listing.room, "ABCDE");
        EXPECT_EQ(listing.host, "Ann");
        EXPECT_EQ(listing.taken, 2);
        EXPECT_EQ(listing.seats, 4);
        EXPECT_FALSE(listing.started);
        Json expected = Json::object();
        expected["hints"] = true;
        EXPECT_EQ(listing.options, expected);

        EXPECT_TRUE(room.RemoveMember(first->player));
        EXPECT_EQ(room.GetListing().host, "Bob");
        EXPECT_EQ(room.GetListing().taken, 1);
    }

}  // namespace BECore::Tests
