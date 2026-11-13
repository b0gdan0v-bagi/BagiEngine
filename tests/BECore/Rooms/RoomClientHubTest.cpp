#include "HubTransport.h"
#include "JudgedTransport.h"

#include <Common/Printers.h>
#include <RoomClient/RoomClient.h>
#include <Rooms/RoomErrors.h>
#include <algorithm>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr milliseconds kStep{10};
        constexpr int kMaxSteps = 20000;
        constexpr int kCardsInAHand = DemoCardGame::kTotalPlays;

        /// A human at the table: a RoomClient over the hub, judged on the wire, that plays the first action it is offered
        struct Player {
            Player(HubWorld& world, bool judged, RoomClientSettings settings = {}) : judge(std::make_shared<WireJudgeLog>()) {
                RoomTransportFactory factory = world.Factory(links);
                settings.transportFactory = judged ? MakeJudgedFactory(std::move(factory), judge) : std::move(factory);
                client = std::make_unique<RoomClient>(std::move(settings));
            }

            [[nodiscard]] size_t Count(RoomClientEventKind kind) const {
                return static_cast<size_t>(std::count_if(events.begin(), events.end(), [kind](const RoomClientEvent& event) { return event.kind == kind; }));
            }

            [[nodiscard]] int TableSize() const {
                const Json* table = FindJsonMember(client->GetView(), "table");
                return table && table->is_array() ? static_cast<int>(table->size()) : -1;
            }

            void TakeEvents() {
                const size_t before = events.size();
                client->DrainEvents(events);
                for (size_t i = before; i < events.size(); ++i) {
                    if (events[i].kind == RoomClientEventKind::Resumed) {
                        // What was sent on the old connection may never have arrived
                        acted = false;
                    }
                }
            }

            /// Plays the first action of the last view, once per version of it
            void ActIfItsTurn() {
                if (!client->IsInRoom() || !client->HasView() || client->IsFinished() || !client->GetActions().is_array() || client->GetActions().empty()) {
                    return;
                }
                if (acted && actedVersion == client->GetViewVersion()) {
                    return;
                }
                actedVersion = client->GetViewVersion();
                acted = true;
                EXPECT_TRUE(client->SendAction(client->GetActions()[0]).has_value());
            }

            HubClientLinks links;
            std::shared_ptr<WireJudgeLog> judge;
            std::unique_ptr<RoomClient> client;
            eastl::vector<RoomClientEvent> events;
            bool acted = false;
            uint64_t actedVersion = 0;
        };

        /// A hub in memory and the clients of a table. The test moves time with Step(): the hub ticks, every client polls and acts
        class RoomClientHubTest : public ::testing::Test {
        protected:
            Player& AddPlayer(bool judged = true, RoomClientSettings settings = {}) {
                _players.push_back(std::make_unique<Player>(World(), judged, std::move(settings)));
                return *_players.back();
            }

            void Step(milliseconds step = kStep) {
                World().Advance(step);
                for (const auto& player : _players) {
                    player->client->Poll(World().Now());
                    player->TakeEvents();
                    player->ActIfItsTurn();
                }
            }

            template <typename Condition>
            bool StepUntil(Condition&& condition, int maxSteps = kMaxSteps) {
                for (int i = 0; i < maxSteps; ++i) {
                    if (condition()) {
                        return true;
                    }
                    Step();
                }
                return condition();
            }

            /// The host makes a room of @p game and everybody else joins it; the client of every player is in the room afterwards
            void MakeLobby(eastl::string_view game, size_t humans, const Json& options = Json::object()) {
                Player& host = AddPlayer();
                ASSERT_TRUE(host.client->Host(kUrl, game, "P0", options).has_value());
                ASSERT_TRUE(StepUntil([&] { return host.client->IsInRoom(); }));
                for (size_t i = 1; i < humans; ++i) {
                    Player& guest = AddPlayer();
                    ASSERT_TRUE(guest.client->Join(kUrl, host.client->GetRoom(), Format("P{}", i).c_str()).has_value());
                    ASSERT_TRUE(StepUntil([&] { return guest.client->IsInRoom(); }));
                }
            }

            [[nodiscard]] bool EveryoneSeesTheEnd() const {
                return std::all_of(_players.begin(), _players.end(), [](const auto& player) { return player->client->IsFinished(); });
            }

            /// The hub of the table; a test that wants other times replaces it before anybody has joined
            HubWorld& World() {
                return *_world;
            }

            void ResetWorld(RoomHubSettings settings) {
                ASSERT_TRUE(_players.empty());
                _world = std::make_unique<HubWorld>(std::move(settings));
            }

            std::unique_ptr<HubWorld> _world = std::make_unique<HubWorld>(HubWorld::MakeSettings(milliseconds(200)));
            std::vector<std::unique_ptr<Player>> _players;
        };

        using Kind = RoomClientEventKind;

    }  // namespace

    TEST_F(RoomClientHubTest, TwoHumansAndTwoComputersPlayAHandAndNobodyPeeks) {
        MakeLobby("cards", 2);
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : _players) {
            EXPECT_EQ(player->TableSize(), kCardsInAHand);
            EXPECT_GT(player->judge->texts, 3u);
            EXPECT_TRUE(player->judge->leaks.empty());
            EXPECT_EQ(player->Count(Kind::Error), 0u);
            EXPECT_EQ(player->Count(Kind::ConnectionLost), 0u);
        }
        EXPECT_EQ(_players[0]->client->GetSeat(), 0);
        EXPECT_EQ(_players[1]->client->GetSeat(), 1);
    }

    TEST_F(RoomClientHubTest, FourHumansPlayAHand) {
        MakeLobby("cards", 4);
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : _players) {
            EXPECT_EQ(player->TableSize(), kCardsInAHand);
            EXPECT_TRUE(player->judge->leaks.empty());
            EXPECT_EQ(player->Count(Kind::Error), 0u);
        }
    }

    TEST_F(RoomClientHubTest, TheJudgeCatchesAGameThatLeaks) {
        // The negative control: the same hand on a game that sends every hand. If the judge could not fail, the tests above would prove nothing
        MakeLobby("cards-leaky", 2);
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        size_t leaks = 0;
        for (const auto& player : _players) {
            leaks += player->judge->leaks.size();
        }
        EXPECT_GT(leaks, 0u);
    }

    TEST_F(RoomClientHubTest, AClientDroppedMidHandReconnectsInsideTheGraceAndPlaysOn) {
        MakeLobby("cards", 2);
        Player& dropped = *_players[1];
        const eastl::string token = dropped.client->GetSession().token;
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return dropped.client->HasView() && dropped.TableSize() >= 2; }));
        const int seat = dropped.client->GetSeat();

        World().Sever(dropped.links.Last());
        Step();
        EXPECT_EQ(dropped.client->GetState(), RoomClientState::Reconnecting);
        EXPECT_EQ(dropped.Count(Kind::ConnectionLost), 1u);

        ASSERT_TRUE(StepUntil([&] { return dropped.client->IsInRoom(); }, 200));
        EXPECT_EQ(dropped.Count(Kind::Resumed), 1u);
        EXPECT_EQ(dropped.client->GetSeat(), seat);
        EXPECT_EQ(dropped.client->GetSession().token, token);
        EXPECT_EQ(dropped.links.links.size(), 2u);

        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : _players) {
            EXPECT_EQ(player->TableSize(), kCardsInAHand);
            EXPECT_TRUE(player->judge->leaks.empty());
        }
    }

    TEST_F(RoomClientHubTest, APublicRoomIsListedAndJoinedFromTheListOnTheSameConnectionAndAHandIsPlayed) {
        Player& host = AddPlayer();
        ASSERT_TRUE(host.client->Host(kUrl, "cards", "P0", Json::object(), true).has_value());
        ASSERT_TRUE(StepUntil([&] { return host.client->IsInRoom(); }));

        Player& browser = AddPlayer();
        ASSERT_TRUE(browser.client->ListRooms(kUrl, "cards").has_value());
        ASSERT_TRUE(StepUntil([&] { return browser.Count(Kind::RoomList) == 1u; }));
        const RoomListMessage& listed = browser.client->GetRoomList();
        ASSERT_EQ(listed.rooms.size(), 1u);
        EXPECT_EQ(listed.rooms[0].room, host.client->GetRoom());
        EXPECT_EQ(listed.rooms[0].host, "P0");
        EXPECT_EQ(listed.rooms[0].taken, 1);
        EXPECT_EQ(listed.rooms[0].seats, DemoCardGame::kSeatCount);
        EXPECT_FALSE(listed.rooms[0].started);
        EXPECT_FALSE(listed.more);
        EXPECT_EQ(browser.client->GetState(), RoomClientState::Connected);

        ASSERT_TRUE(browser.client->Join(kUrl, listed.rooms[0].room, "P1").has_value());
        ASSERT_TRUE(StepUntil([&] { return browser.client->IsInRoom(); }));
        EXPECT_EQ(browser.links.links.size(), 1u);
        EXPECT_EQ(browser.client->GetRoom(), host.client->GetRoom());

        ASSERT_TRUE(host.client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : _players) {
            EXPECT_EQ(player->TableSize(), kCardsInAHand);
            EXPECT_TRUE(player->judge->leaks.empty());
            EXPECT_EQ(player->Count(Kind::Error), 0u);
        }
    }

    TEST_F(RoomClientHubTest, APrivateRoomIsNotSeenByABrowser) {
        Player& host = AddPlayer();
        ASSERT_TRUE(host.client->Host(kUrl, "cards", "P0").has_value());
        ASSERT_TRUE(StepUntil([&] { return host.client->IsInRoom(); }));

        Player& browser = AddPlayer();
        ASSERT_TRUE(browser.client->ListRooms(kUrl, "cards").has_value());
        ASSERT_TRUE(StepUntil([&] { return browser.Count(Kind::RoomList) == 1u; }));
        EXPECT_TRUE(browser.client->GetRoomList().rooms.empty());
        EXPECT_EQ(browser.Count(Kind::Error), 0u);
    }

    TEST_F(RoomClientHubTest, ABrowserThatAsksTooOftenGetsAnErrorAndKeepsItsConnection) {
        Player& browser = AddPlayer();
        ASSERT_TRUE(browser.client->ListRooms(kUrl, "cards").has_value());
        ASSERT_TRUE(StepUntil([&] { return browser.Count(Kind::RoomList) == 1u; }));
        // The burst is spent at once; the next list of the same moment is refused, the connection stays
        for (int i = 0; i < RoomLimits::kListBurst; ++i) {
            ASSERT_TRUE(browser.client->ListRooms(kUrl, "cards").has_value());
        }
        Step();
        ASSERT_GE(browser.Count(Kind::Error), 1u);
        const auto refused = std::find_if(browser.events.begin(), browser.events.end(), [](const RoomClientEvent& event) { return event.kind == Kind::Error; });
        EXPECT_EQ(refused->code, RoomErrors::kRateLimited);
        EXPECT_EQ(refused->ref, RoomMessageTypes::kListRooms);
        EXPECT_EQ(browser.client->GetState(), RoomClientState::Connected);
        EXPECT_EQ(browser.links.links.size(), 1u);
    }

    TEST_F(RoomClientHubTest, AClientThatReturnsAfterTheGraceFindsAComputerHasPlayedItsSeat) {
        ResetWorld(HubWorld::MakeSettings(seconds(1)));
        // Seat 0 moves first, so the hand stalls at seat 1 as soon as the client of seat 1 is gone
        MakeLobby("cards", 2, Json::object({{"firstSeat", 0}}));
        Player& away = *_players[1];
        Player& other = *_players[0];
        const eastl::string token = away.client->GetSession().token;
        ASSERT_TRUE(other.client->Start().has_value());
        // Seat 1 is cut off before the first move and kept away
        World().SetRefusing(true);
        World().Sever(away.links.Last());
        Step();
        ASSERT_EQ(away.client->GetState(), RoomClientState::Reconnecting);

        // Inside the grace nobody plays the seat; the turn comes to it and the hand stalls
        for (int i = 0; i < 2500; ++i) {
            Step();
        }
        const auto playsOfSeat = [&](const Player& player, int seat) {
            int plays = 0;
            const Json* table = FindJsonMember(player.client->GetView(), "table");
            for (size_t i = 0; table && table->is_array() && i < table->size(); ++i) {
                plays += GetJsonInt((*table)[i], "seat").value_or(-1) == seat ? 1 : 0;
            }
            return plays;
        };
        EXPECT_EQ(playsOfSeat(other, 1), 0) << "nobody acts for a seat inside the grace";
        EXPECT_FALSE(other.client->IsFinished());

        // Past the grace a computer plays it; the hand moves on without the client
        ASSERT_TRUE(StepUntil([&] { return playsOfSeat(other, 1) > 0; }, 4000));
        const int playedWhileAway = playsOfSeat(other, 1);
        EXPECT_EQ(away.Count(Kind::Resumed), 0u);

        // The server is back: the client returns with the same token and takes its seat back
        World().SetRefusing(false);
        ASSERT_TRUE(StepUntil([&] { return away.client->IsInRoom(); }, 2000));
        EXPECT_EQ(away.Count(Kind::Resumed), 1u);
        EXPECT_EQ(away.client->GetSeat(), 1);
        EXPECT_EQ(away.client->GetSession().token, token);
        EXPECT_GE(playsOfSeat(away, 1), playedWhileAway);

        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
        for (const auto& player : _players) {
            EXPECT_EQ(player->TableSize(), kCardsInAHand);
            EXPECT_TRUE(player->judge->leaks.empty());
        }
    }

    TEST_F(RoomClientHubTest, ASilentConnectionIsDroppedAndBroughtBack) {
        MakeLobby("cards", 2);
        Player& quiet = *_players[1];
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return quiet.client->HasView(); }));

        // The network eats everything in both directions and tells nobody
        quiet.links.Last().blackhole = true;
        ASSERT_TRUE(StepUntil([&] { return quiet.client->GetState() == RoomClientState::Reconnecting; }, 4000));
        EXPECT_EQ(quiet.Count(Kind::ConnectionLost), 1u);
        ASSERT_TRUE(StepUntil([&] { return quiet.client->IsInRoom(); }, 1000));
        EXPECT_EQ(quiet.Count(Kind::Resumed), 1u);
        ASSERT_TRUE(StepUntil([&] { return EveryoneSeesTheEnd(); }));
    }

    TEST_F(RoomClientHubTest, AnotherDeviceWithTheSessionTakesTheSeatAndTheFirstOneStaysOut) {
        MakeLobby("cards", 2);
        Player& first = *_players[1];
        ASSERT_TRUE(_players[0]->client->Start().has_value());
        ASSERT_TRUE(StepUntil([&] { return first.client->HasView(); }));
        const RoomSession session = first.client->GetSession();

        Player& second = AddPlayer();
        ASSERT_TRUE(second.client->Resume(session).has_value());
        ASSERT_TRUE(StepUntil([&] { return second.client->IsInRoom(); }, 200));
        EXPECT_EQ(second.Count(Kind::Resumed), 1u);
        EXPECT_EQ(second.client->GetSeat(), 1);

        // The first one is told it was replaced and does not reconnect to fight for the seat
        ASSERT_TRUE(StepUntil([&] { return first.client->GetState() == RoomClientState::Disconnected; }, 200));
        EXPECT_EQ(first.Count(Kind::Left), 1u);
        const auto closed = std::find_if(first.events.begin(), first.events.end(), [](const RoomClientEvent& event) { return event.kind == Kind::Closed; });
        ASSERT_NE(closed, first.events.end());
        EXPECT_EQ(closed->code, RoomClientCloseReasons::kReplaced);
        const size_t links = first.links.links.size();
        for (int i = 0; i < 1000; ++i) {
            Step();
        }
        EXPECT_EQ(first.links.links.size(), links);

        // The second plays the seat for the rest of the hand (the first has stopped acting)
        ASSERT_TRUE(StepUntil([&] { return _players[0]->client->IsFinished() && second.client->IsFinished(); }));
        EXPECT_EQ(second.TableSize(), kCardsInAHand);
        EXPECT_TRUE(second.judge->leaks.empty());
    }

    TEST_F(RoomClientHubTest, ASessionOfARoomThatNoLongerExistsEndsWithRoomGone) {
        MakeLobby("cards", 2);
        const RoomSession session = _players[1]->client->GetSession();
        // The only members leave the lobby: the room is closed
        ASSERT_TRUE(_players[1]->client->Leave().has_value());
        ASSERT_TRUE(_players[0]->client->Leave().has_value());
        Step();
        Step();
        ASSERT_EQ(World().Hub().GetRoomCount(), 0u);

        Player& late = AddPlayer(false);
        ASSERT_TRUE(late.client->Resume(session).has_value());
        ASSERT_TRUE(StepUntil([&] { return late.client->GetState() == RoomClientState::Disconnected; }, 200));
        ASSERT_FALSE(late.events.empty());
        EXPECT_EQ(late.events.back().kind, Kind::Closed);
        EXPECT_EQ(late.events.back().code, RoomClientCloseReasons::kRoomGone);
        EXPECT_FALSE(late.client->GetSession().IsValid());
    }

}  // namespace BECore::Tests
