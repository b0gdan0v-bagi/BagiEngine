#include "FakeSeatedGame.h"
#include "TestRandom.h"

#include <Common/Printers.h>
#include <Rooms/Room.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const RoomTime kNow{std::chrono::seconds(1000)};

        RoomTime After(std::chrono::milliseconds delay) {
            return kNow + delay;
        }

        /// A room over the fake game, and the way to look at the game the room made (the truth, for the leak check)
        struct TestRoom {
            Room room;
            eastl::shared_ptr<FakeSeatedGame*> game;
            Vector<RoomMemberJoined> members;

            const FakeSeatedGame& Game() const {
                return **game;
            }
        };

        TestRoom MakeTestRoom(int humans, RoomTimings timings = {std::chrono::seconds(30), std::chrono::minutes(10), std::chrono::milliseconds(0)}) {
            auto game = eastl::make_shared<FakeSeatedGame*>(nullptr);
            SeatedGameFactory factory = [game]() -> UniquePtr<ISeatedGame> {
                auto made = eastl::make_unique<FakeSeatedGame>();
                *game = made.get();
                return made;
            };
            TestRoom test{Room("ABCDE", SeatedGameInfo{"fake", FakeSeatedGame::kSeatCount}, MakeSequence(3), std::move(factory), timings), game, {}};
            for (int i = 0; i < humans; ++i) {
                auto joined = test.room.AddMember(Format("P{}", i), kNow);
                EXPECT_TRUE(joined.has_value());
                test.members.push_back(joined ? *joined : RoomMemberJoined{});
            }
            return test;
        }

        Json FirstSeatOptions(int seat) {
            Json options = Json::object();
            options["firstSeat"] = seat;
            return options;
        }

        String CodeOf(const std::expected<void, ControlError>& result) {
            return result ? String("<ok>") : result.error().code;
        }

        String CodeOf(const std::expected<Json, ControlError>& result) {
            return result ? String("<ok>") : result.error().code;
        }

        Json PlayAction(const String& card) {
            Json action = Json::object();
            action["type"] = "play";
            action["card"] = card.c_str();
            return action;
        }

        /// Every member's view and actions, judged against the game as it is now
        void ExpectViewsDoNotLeak(const TestRoom& test) {
            for (const RoomMemberJoined& member : test.members) {
                if (!test.room.HasMember(member.player)) {
                    continue;
                }
                const auto view = test.room.GetSeatView(member.player);
                ASSERT_TRUE(view.has_value());
                ExpectNoLeak(DumpJson(*view), member.seat, test.Game());
                const auto actions = test.room.GetSeatActions(member.player);
                ASSERT_TRUE(actions.has_value());
                ExpectNoLeak(DumpJson(*actions), member.seat, test.Game());
            }
        }

    }  // namespace

    TEST(RoomPlayTest, OnlyTheHostStartsAndOnlyOnce) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId host = test.members[0].player;
        const PlayerId guest = test.members[1].player;
        EXPECT_EQ(CodeOf(test.room.Start(guest, kNow)), RoomErrors::kNotHost);
        EXPECT_EQ(CodeOf(test.room.Start(99, kNow)), RoomErrors::kNotInRoom);
        EXPECT_EQ(test.room.GetPhase(), RoomPhase::Lobby);

        const uint64_t revision = test.room.GetLobbyRevision();
        EXPECT_EQ(CodeOf(test.room.Start(host, kNow, FirstSeatOptions(0))), "<ok>");
        EXPECT_EQ(test.room.GetPhase(), RoomPhase::Playing);
        EXPECT_GT(test.room.GetLobbyRevision(), revision);
        EXPECT_EQ(CodeOf(test.room.Start(host, kNow)), RoomErrors::kGameStarted);
        EXPECT_EQ(test.room.AddMember("Late", kNow).error().code, RoomErrors::kGameStarted);
        EXPECT_EQ(CodeOf(test.room.ChooseSeat(guest, 3)), RoomErrors::kGameStarted);
        EXPECT_FALSE(test.room.RemoveMember(guest));
    }

    TEST(RoomPlayTest, EmptySeatsBecomeComputers) {
        TestRoom test = MakeTestRoom(2);
        for (int seat = 0; seat < 4; ++seat) {
            EXPECT_FALSE(test.room.IsSeatComputerControlled(seat, kNow));
        }
        ASSERT_TRUE(test.room.Start(test.members[0].player, kNow, FirstSeatOptions(0)).has_value());
        EXPECT_FALSE(test.room.IsSeatComputerControlled(0, kNow));
        EXPECT_FALSE(test.room.IsSeatComputerControlled(1, kNow));
        EXPECT_TRUE(test.room.IsSeatComputerControlled(2, kNow));
        EXPECT_TRUE(test.room.IsSeatComputerControlled(3, kNow));
        EXPECT_FALSE(test.room.IsSeatComputerControlled(4, kNow));
        EXPECT_FALSE(test.room.IsSeatComputerControlled(-1, kNow));
        const LobbySnapshot lobby = test.room.GetLobbySnapshot();
        EXPECT_EQ(lobby.phase, RoomPhase::Playing);
        EXPECT_EQ(lobby.seats[1].kind, SeatKind::Human);
        EXPECT_EQ(lobby.seats[2].kind, SeatKind::Computer);
        EXPECT_EQ(lobby.seats[3].kind, SeatKind::Computer);
    }

    TEST(RoomPlayTest, NothingToDoBeforeTheStart) {
        TestRoom test = MakeTestRoom(1);
        const PlayerId player = test.members[0].player;
        EXPECT_EQ(CodeOf(test.room.PerformAction(player, 0, PlayAction("c0_0"))), RoomErrors::kNotStarted);
        EXPECT_EQ(CodeOf(test.room.GetSeatView(player)), RoomErrors::kNotStarted);
        EXPECT_EQ(CodeOf(test.room.GetSeatActions(player)), RoomErrors::kNotStarted);
        EXPECT_EQ(CodeOf(test.room.GetSeatView(77)), RoomErrors::kNotInRoom);
        EXPECT_FALSE(test.room.StepComputers(kNow));
        EXPECT_FALSE(test.room.MarkAway(player, kNow));
        EXPECT_EQ(test.room.GetGameVersion(), 0u);
    }

    TEST(RoomPlayTest, StartFailuresLeaveTheRoomInTheLobby) {
        // No factory
        Room bare("ABCDE", SeatedGameInfo{"fake", 4}, MakeSequence(1));
        const auto joined = bare.AddMember("Ann", kNow);
        ASSERT_TRUE(joined.has_value());
        EXPECT_EQ(CodeOf(bare.Start(joined->player, kNow)), RoomErrors::kInternalError);
        EXPECT_EQ(bare.GetPhase(), RoomPhase::Lobby);

        // A factory that gives nothing, and one whose game has another seat count than the registry said
        Room empty("ABCDE", SeatedGameInfo{"fake", 4}, MakeSequence(1), []() -> UniquePtr<ISeatedGame> { return nullptr; });
        const auto emptyHost = empty.AddMember("Ann", kNow);
        ASSERT_TRUE(emptyHost.has_value());
        EXPECT_EQ(CodeOf(empty.Start(emptyHost->player, kNow)), RoomErrors::kInternalError);
        EXPECT_EQ(empty.GetPhase(), RoomPhase::Lobby);

        Room mismatch("ABCDE", SeatedGameInfo{"fake", 3}, MakeSequence(1), []() -> UniquePtr<ISeatedGame> { return eastl::make_unique<FakeSeatedGame>(); });
        const auto mismatchHost = mismatch.AddMember("Ann", kNow);
        ASSERT_TRUE(mismatchHost.has_value());
        EXPECT_EQ(CodeOf(mismatch.Start(mismatchHost->player, kNow)), RoomErrors::kInternalError);
        EXPECT_EQ(mismatch.GetPhase(), RoomPhase::Lobby);

        // The game refuses the options: the error is passed on, and a second try with good ones works
        TestRoom test = MakeTestRoom(1);
        const uint64_t revision = test.room.GetLobbyRevision();
        EXPECT_EQ(CodeOf(test.room.Start(test.members[0].player, kNow, FirstSeatOptions(9))), ControlErrors::kInvalidParams);
        EXPECT_EQ(test.room.GetPhase(), RoomPhase::Lobby);
        EXPECT_EQ(test.room.GetLobbyRevision(), revision);
        EXPECT_EQ(test.room.GetGameVersion(), 0u);
        EXPECT_EQ(CodeOf(test.room.Start(test.members[0].player, kNow, FirstSeatOptions(1))), "<ok>");
    }

    TEST(RoomPlayTest, TurnsStaleAndIllegalActions) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        ExpectViewsDoNotLeak(test);

        const uint64_t version = test.room.GetGameVersion();
        EXPECT_GT(version, 0u);
        const auto actions = test.room.GetSeatActions(a);
        ASSERT_TRUE(actions.has_value());
        EXPECT_EQ(actions->size(), 3u);
        EXPECT_TRUE(test.room.GetSeatActions(b)->empty());

        // Off turn, a card of another seat, a malformed action, a stale version: nothing changes
        EXPECT_EQ(CodeOf(test.room.PerformAction(b, version, PlayAction("c1_0"))), RoomErrors::kNotYourTurn);
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, version, PlayAction("c1_0"))), RoomErrors::kIllegalAction);
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, version, Json::object())), ControlErrors::kInvalidParams);
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, version + 1, PlayAction("c0_0"))), RoomErrors::kStaleAction);
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, 0, PlayAction("c0_0"))), RoomErrors::kStaleAction);
        EXPECT_EQ(CodeOf(test.room.PerformAction(88, version, PlayAction("c0_0"))), RoomErrors::kNotInRoom);
        EXPECT_EQ(test.room.GetGameVersion(), version);
        EXPECT_EQ(test.Game().GetPlayCount(), 0);

        const auto outcome = test.room.PerformAction(a, version, PlayAction("c0_1"));
        ASSERT_TRUE(outcome.has_value());
        EXPECT_GT(test.room.GetGameVersion(), version);
        EXPECT_EQ(test.Game().GetPlayCount(), 1);
        // The old version is stale now
        EXPECT_EQ(CodeOf(test.room.PerformAction(b, version, PlayAction("c1_0"))), RoomErrors::kStaleAction);
        ExpectViewsDoNotLeak(test);
    }

    TEST(RoomPlayTest, AnAbsentPlayerKeepsTheSeatForTheGraceThenAComputerPlaysIt) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        const uint64_t revision = test.room.GetLobbyRevision();
        ASSERT_TRUE(test.room.MarkAway(a, kNow));
        EXPECT_GT(test.room.GetLobbyRevision(), revision);
        EXPECT_FALSE(test.room.GetLobbySnapshot().seats[0].connected);

        // Inside the grace nobody acts: not a computer, not the absent player
        const auto inside = After(std::chrono::seconds(29));
        EXPECT_FALSE(test.room.IsSeatComputerControlled(0, inside));
        EXPECT_FALSE(test.room.StepComputers(inside));
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, test.room.GetGameVersion(), PlayAction("c0_0"))), RoomErrors::kNotYourTurn);
        EXPECT_EQ(test.Game().GetPlayCount(), 0);

        // After the grace a computer plays the lowest card of the seat
        const auto after = After(std::chrono::seconds(30));
        EXPECT_TRUE(test.room.IsSeatComputerControlled(0, after));
        EXPECT_TRUE(test.room.StepComputers(after));
        EXPECT_EQ(test.Game().GetPlayCount(), 1);
        EXPECT_EQ(test.Game().GetHand(0).size(), 2u);
        ExpectViewsDoNotLeak(test);
    }

    TEST(RoomPlayTest, TheReturnTakesControlBackAtOnceAndChecksTheToken) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        ASSERT_TRUE(test.room.MarkAway(a, kNow));
        const auto late = After(std::chrono::seconds(45));
        ASSERT_TRUE(test.room.IsSeatComputerControlled(0, late));

        EXPECT_EQ(CodeOf(test.room.MarkReturned(a, "")), RoomErrors::kBadToken);
        // The token of another member is not a key to this seat
        EXPECT_EQ(CodeOf(test.room.MarkReturned(a, test.members[1].token)), RoomErrors::kBadToken);
        EXPECT_EQ(CodeOf(test.room.MarkReturned(99, test.members[0].token)), RoomErrors::kNotInRoom);
        EXPECT_TRUE(test.room.IsSeatComputerControlled(0, late));

        const uint64_t revision = test.room.GetLobbyRevision();
        EXPECT_EQ(CodeOf(test.room.MarkReturned(a, test.members[0].token)), "<ok>");
        EXPECT_GT(test.room.GetLobbyRevision(), revision);
        EXPECT_FALSE(test.room.IsSeatComputerControlled(0, late));
        EXPECT_FALSE(test.room.StepComputers(late));
        EXPECT_TRUE(test.room.GetLobbySnapshot().seats[0].connected);
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, test.room.GetGameVersion(), PlayAction("c0_0"))), "<ok>");
        EXPECT_EQ(test.Game().GetPlayCount(), 1);
        // A return that changes nothing does not move the revision
        const uint64_t after = test.room.GetLobbyRevision();
        EXPECT_EQ(CodeOf(test.room.MarkReturned(b, test.members[1].token)), "<ok>");
        EXPECT_EQ(test.room.GetLobbyRevision(), after);
    }

    TEST(RoomPlayTest, ALeftPlayersSeatPlaysOnForGood) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        EXPECT_TRUE(test.room.Leave(a));
        EXPECT_FALSE(test.room.Leave(a));
        EXPECT_FALSE(test.room.HasMember(a));
        EXPECT_FALSE(test.room.FindByToken(test.members[0].token).has_value());
        EXPECT_EQ(CodeOf(test.room.MarkReturned(a, test.members[0].token)), RoomErrors::kNotInRoom);
        EXPECT_TRUE(test.room.IsHost(b));
        EXPECT_TRUE(test.room.IsSeatComputerControlled(0, kNow));
        EXPECT_EQ(test.room.GetLobbySnapshot().seats[0].kind, SeatKind::Computer);

        EXPECT_TRUE(test.room.StepComputers(kNow));
        EXPECT_EQ(test.Game().GetPlayCount(), 1);
        EXPECT_EQ(test.Game().GetTurn(), 1);
    }

    TEST(RoomPlayTest, LeavingInTheLobbyRemovesTheMember) {
        TestRoom test = MakeTestRoom(2);
        EXPECT_TRUE(test.room.Leave(test.members[0].player));
        EXPECT_EQ(test.room.GetMemberCount(), 1u);
        EXPECT_TRUE(test.room.IsHost(test.members[1].player));
        EXPECT_EQ(test.room.GetLobbySnapshot().seats[0].kind, SeatKind::Empty);
    }

    TEST(RoomPlayTest, AFullGameWithComputersFinishes) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        ASSERT_TRUE(test.room.Start(a, kNow).has_value());

        RoomTime now = kNow;
        for (int step = 0; step < 200 && test.room.GetPhase() == RoomPhase::Playing; ++step) {
            now += std::chrono::seconds(1);
            ExpectViewsDoNotLeak(test);
            // The people play the lowest card on their turn, the computers fill in the rest
            for (const PlayerId player : {a, b}) {
                const auto actions = test.room.GetSeatActions(player);
                ASSERT_TRUE(actions.has_value());
                if (!actions->empty()) {
                    const auto result = test.room.PerformAction(player, test.room.GetGameVersion(), (*actions)[0]);
                    EXPECT_TRUE(result.has_value());
                    ExpectViewsDoNotLeak(test);
                }
            }
            test.room.StepComputers(now);
            ExpectViewsDoNotLeak(test);
        }
        EXPECT_EQ(test.room.GetPhase(), RoomPhase::Finished);
        EXPECT_EQ(test.Game().GetPlayCount(), FakeSeatedGame::kTotalPlays);
        EXPECT_EQ(test.room.GetLobbySnapshot().phase, RoomPhase::Finished);
        EXPECT_TRUE(test.room.GetSeatActions(a)->empty());
        EXPECT_FALSE(test.room.StepComputers(now + std::chrono::seconds(5)));
        EXPECT_EQ(CodeOf(test.room.PerformAction(a, test.room.GetGameVersion(), PlayAction("c0_0"))), RoomErrors::kNotYourTurn);
    }

    TEST(RoomPlayTest, ComputersWaitForTheDelay) {
        const RoomTimings timings{std::chrono::seconds(30), std::chrono::minutes(10), std::chrono::milliseconds(800)};
        TestRoom test = MakeTestRoom(1, timings);
        ASSERT_TRUE(test.room.Start(test.members[0].player, kNow, FirstSeatOptions(2)).has_value());

        EXPECT_FALSE(test.room.StepComputers(kNow));
        EXPECT_FALSE(test.room.StepComputers(After(std::chrono::milliseconds(799))));
        EXPECT_EQ(test.Game().GetPlayCount(), 0);
        EXPECT_TRUE(test.room.StepComputers(After(std::chrono::milliseconds(800))));
        EXPECT_EQ(test.Game().GetPlayCount(), 1);
        // The next computer (seat 3) waits 800 ms from the last move
        EXPECT_FALSE(test.room.StepComputers(After(std::chrono::milliseconds(800))));
        EXPECT_FALSE(test.room.StepComputers(After(std::chrono::milliseconds(1599))));
        EXPECT_TRUE(test.room.StepComputers(After(std::chrono::milliseconds(1600))));
        EXPECT_EQ(test.Game().GetPlayCount(), 2);
        // Seat 0 is a person's turn now: no computer acts however long it waits
        EXPECT_FALSE(test.room.StepComputers(After(std::chrono::hours(1))));
        // Time that goes back does not act either
        EXPECT_FALSE(test.room.StepComputers(kNow));
    }

    TEST(RoomPlayTest, ShouldCloseCases) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        EXPECT_FALSE(test.room.ShouldClose(kNow));
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        EXPECT_FALSE(test.room.ShouldClose(kNow));

        // All away: not closed before the timeout, closed at it; one still present keeps it open
        ASSERT_TRUE(test.room.MarkAway(a, kNow));
        EXPECT_FALSE(test.room.ShouldClose(After(std::chrono::hours(1))));
        const auto awayB = After(std::chrono::minutes(5));
        ASSERT_TRUE(test.room.MarkAway(b, awayB));
        EXPECT_FALSE(test.room.ShouldClose(After(std::chrono::minutes(10))));
        EXPECT_FALSE(test.room.ShouldClose(awayB + std::chrono::minutes(10) - std::chrono::milliseconds(1)));
        EXPECT_TRUE(test.room.ShouldClose(awayB + std::chrono::minutes(10)));

        // A return resets the clock
        ASSERT_TRUE(test.room.MarkReturned(a, test.members[0].token).has_value());
        EXPECT_FALSE(test.room.ShouldClose(awayB + std::chrono::hours(1)));
        ASSERT_TRUE(test.room.MarkAway(a, awayB + std::chrono::hours(1)));
        EXPECT_FALSE(test.room.ShouldClose(awayB + std::chrono::hours(1) + std::chrono::minutes(9)));
        EXPECT_TRUE(test.room.ShouldClose(awayB + std::chrono::hours(1) + std::chrono::minutes(10)));

        // All left
        EXPECT_TRUE(test.room.Leave(a));
        EXPECT_TRUE(test.room.Leave(b));
        EXPECT_TRUE(test.room.ShouldClose(kNow));
    }

    TEST(RoomPlayTest, AnEmptyLobbyShouldClose) {
        TestRoom test = MakeTestRoom(1);
        EXPECT_FALSE(test.room.ShouldClose(After(std::chrono::hours(5))));
        EXPECT_TRUE(test.room.RemoveMember(test.members[0].player));
        EXPECT_TRUE(test.room.ShouldClose(kNow));
    }

    TEST(RoomPlayTest, AnExhaustedRandomGivesInternalErrorNotAHang) {
        auto constant = []() -> uint64_t { return 42; };
        Room room("ABCDE", SeatedGameInfo{"fake", 4}, constant);
        ASSERT_TRUE(room.AddMember("Ann", kNow).has_value());
        const auto second = room.AddMember("Bob", kNow);
        ASSERT_FALSE(second.has_value());
        EXPECT_EQ(second.error().code, RoomErrors::kInternalError);
        EXPECT_EQ(room.GetMemberCount(), 1u);
    }

    namespace {

        /// A game whose computer proposes an action the game itself refuses (a bug of an adapter)
        class StubbornGame : public FakeSeatedGame {
        public:
            Optional<Json> ChooseComputerAction(int seat) const override {
                ++proposals;
                return PlayAction(Format("c{}_0", (seat + 1) % kSeatCount));
            }

            mutable int proposals = 0;
        };

        /// Collects what LogEvent::Flush() delivers, as copies (the views point into the ring buffer)
        class LogRecorder : public SubscriptionHolder {
        public:
            LogRecorder() {
                Subscribe<LogEvent, &LogRecorder::OnLog>(this);
            }

            const Vector<String>& Errors() const {
                return _errors;
            }

        private:
            void OnLog(const LogEvent& event) {
                if (event.level == LogLevel::Error) {
                    _errors.emplace_back(event.message.data(), event.message.size());
                }
            }

            Vector<String> _errors;
        };

    }  // namespace

    TEST(RoomPlayTest, ARefusedComputerMoveIsNotRetriedAndChangesNothing) {
        auto game = eastl::make_shared<StubbornGame*>(nullptr);
        SeatedGameFactory factory = [game]() -> UniquePtr<ISeatedGame> {
            auto made = eastl::make_unique<StubbornGame>();
            *game = made.get();
            return made;
        };
        Room room("ABCDE", SeatedGameInfo{"fake", 4}, MakeSequence(5), std::move(factory), RoomTimings{std::chrono::seconds(30), std::chrono::minutes(10), std::chrono::milliseconds(0)});
        const auto joined = room.AddMember("Ann", kNow);
        ASSERT_TRUE(joined.has_value());
        ASSERT_TRUE(room.Start(joined->player, kNow, FirstSeatOptions(1)).has_value());
        const uint64_t version = room.GetGameVersion();
        // Seat 1 is a computer and has the turn; its proposal is refused: logged, no move, no loop
        LogRecorder log;
        EXPECT_FALSE(room.StepComputers(kNow));
        LogEvent::Flush();
        ASSERT_EQ(log.Errors().size(), 1u);
        EXPECT_NE(log.Errors()[0].find("seat 1"), String::npos) << log.Errors()[0].c_str();
        EXPECT_EQ(log.Errors()[0].find("ABCDE"), String::npos) << "the room code is not logged: " << log.Errors()[0].c_str();
        EXPECT_EQ((*game)->proposals, 1);
        EXPECT_EQ(room.GetGameVersion(), version);
        EXPECT_EQ(room.GetPhase(), RoomPhase::Playing);
    }

    TEST(RoomPlayTest, AReturningMemberBecomesHostWhenNoConnectedHumanWasLeft) {
        TestRoom test = MakeTestRoom(2);
        const PlayerId a = test.members[0].player;
        const PlayerId b = test.members[1].player;
        ASSERT_TRUE(test.room.Start(a, kNow, FirstSeatOptions(0)).has_value());
        ASSERT_TRUE(test.room.MarkAway(b, kNow));
        ASSERT_TRUE(test.room.Leave(a));
        // The host left and nobody connected was there to take over
        EXPECT_FALSE(test.room.IsHost(a));
        EXPECT_FALSE(test.room.IsHost(b));
        ASSERT_TRUE(test.room.MarkReturned(b, test.members[1].token).has_value());
        EXPECT_TRUE(test.room.IsHost(b));
    }

}  // namespace BECore::Tests
