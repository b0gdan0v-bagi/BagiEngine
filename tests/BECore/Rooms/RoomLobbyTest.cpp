#include <Common/Printers.h>
#include <Rooms/Room.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        const RoomTime kNow{std::chrono::seconds(1000)};

        /// A deterministic stream of well-mixed words (splitmix64)
        RandomFn MakeSequence(uint64_t seed) {
            auto state = std::make_shared<uint64_t>(seed);
            return [state]() -> uint64_t {
                uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return z ^ (z >> 31);
            };
        }

        Room MakeRoom(int seatCount = 4) {
            return Room("ABCDE", SeatedGameInfo{"fake", seatCount}, MakeSequence(7));
        }

        RoomMemberJoined Join(Room& room, eastl::string_view name) {
            auto joined = room.AddMember(name, kNow);
            EXPECT_TRUE(joined.has_value());
            return joined ? *joined : RoomMemberJoined{};
        }

        eastl::string CodeOf(const std::expected<void, ControlError>& result) {
            return result ? eastl::string("<ok>") : result.error().code;
        }

    }  // namespace

    TEST(RoomLobbyTest, JoinFillsSeatsInOrderAndTheFirstIsHost) {
        Room room = MakeRoom();
        EXPECT_TRUE(room.IsEmpty());
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        const auto c = Join(room, "Cy");
        EXPECT_EQ(a.seat, 0);
        EXPECT_EQ(b.seat, 1);
        EXPECT_EQ(c.seat, 2);
        EXPECT_NE(a.player, b.player);
        EXPECT_TRUE(room.IsHost(a.player));
        EXPECT_FALSE(room.IsHost(b.player));
        EXPECT_FALSE(room.IsHost(0));
        EXPECT_EQ(room.GetPhase(), RoomPhase::Lobby);
        EXPECT_EQ(room.GetMemberCount(), 3u);
        EXPECT_EQ(room.GetPlayerAt(1), b.player);
        EXPECT_EQ(room.GetSeatOf(c.player), 2);
        EXPECT_FALSE(room.GetPlayerAt(3).has_value());
    }

    TEST(RoomLobbyTest, TheFifthJoinIsRoomFull) {
        Room room = MakeRoom();
        for (int i = 0; i < 4; ++i) {
            Join(room, "P");
        }
        const auto revision = room.GetLobbyRevision();
        const auto joined = room.AddMember("Late", kNow);
        ASSERT_FALSE(joined.has_value());
        EXPECT_EQ(joined.error().code, RoomErrors::kRoomFull);
        EXPECT_EQ(room.GetLobbyRevision(), revision);
    }

    TEST(RoomLobbyTest, NamesAreCheckedAndMayRepeat) {
        Room room = MakeRoom();
        EXPECT_EQ(room.AddMember("", kNow).error().code, RoomErrors::kBadName);
        EXPECT_EQ(room.AddMember("   ", kNow).error().code, RoomErrors::kBadName);
        EXPECT_EQ(room.AddMember("a\nb", kNow).error().code, RoomErrors::kBadName);
        EXPECT_TRUE(room.IsEmpty());
        Join(room, "  Ann ");
        Join(room, "Ann");
        const LobbySnapshot lobby = room.GetLobbySnapshot();
        EXPECT_EQ(lobby.seats[0].name, "Ann");
        EXPECT_EQ(lobby.seats[1].name, "Ann");
    }

    TEST(RoomLobbyTest, ChooseSeatMovesToAFreeSeat) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, 3)), "<ok>");
        EXPECT_EQ(room.GetSeatOf(b.player), 3);
        EXPECT_FALSE(room.GetPlayerAt(1).has_value());
        // The freed seat is the lowest free one for the next to join
        EXPECT_EQ(Join(room, "Cy").seat, 1);
        EXPECT_EQ(room.GetSeatOf(a.player), 0);
    }

    TEST(RoomLobbyTest, ChooseSeatErrors) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, 0)), RoomErrors::kSeatTaken);
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, 4)), RoomErrors::kInvalidSeat);
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, -1)), RoomErrors::kInvalidSeat);
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, INT64_MIN)), RoomErrors::kInvalidSeat);
        EXPECT_EQ(CodeOf(room.ChooseSeat(b.player, INT64_MAX)), RoomErrors::kInvalidSeat);
        EXPECT_EQ(CodeOf(room.ChooseSeat(99, 2)), RoomErrors::kNotInRoom);
        EXPECT_EQ(room.GetSeatOf(a.player), 0);
        EXPECT_EQ(room.GetSeatOf(b.player), 1);
    }

    TEST(RoomLobbyTest, HostPassesToTheLowestSeatWhenHostLeaves) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        const auto c = Join(room, "Cy");
        ASSERT_TRUE(room.ChooseSeat(b.player, 3).has_value());
        EXPECT_TRUE(room.RemoveMember(a.player));
        EXPECT_TRUE(room.IsHost(c.player));  // seat 2 is below seat 3
        EXPECT_FALSE(room.IsHost(b.player));
        EXPECT_EQ(room.GetLobbySnapshot().hostSeat, 2);
        EXPECT_TRUE(room.RemoveMember(c.player));
        EXPECT_TRUE(room.IsHost(b.player));
        EXPECT_TRUE(room.RemoveMember(b.player));
        EXPECT_TRUE(room.IsEmpty());
        EXPECT_EQ(room.GetLobbySnapshot().hostSeat, -1);
    }

    TEST(RoomLobbyTest, LeavingANonHostKeepsTheHost) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        EXPECT_TRUE(room.RemoveMember(b.player));
        EXPECT_TRUE(room.IsHost(a.player));
    }

    TEST(RoomLobbyTest, RemovalFreesTheSeatAndUnknownPlayersAreRefused) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        EXPECT_TRUE(room.RemoveMember(a.player));
        EXPECT_FALSE(room.HasMember(a.player));
        EXPECT_FALSE(room.RemoveMember(a.player));
        EXPECT_FALSE(room.RemoveMember(77));
        EXPECT_EQ(Join(room, "Cy").seat, 0);
        EXPECT_EQ(room.GetSeatOf(b.player), 1);
        // A player number is never reused
        EXPECT_NE(Join(room, "Di").player, a.player);
    }

    TEST(RoomLobbyTest, RevisionGrowsOnChangesAndNotOnNoOps) {
        Room room = MakeRoom();
        uint64_t revision = room.GetLobbyRevision();
        const auto a = Join(room, "Ann");
        EXPECT_GT(room.GetLobbyRevision(), revision);
        revision = room.GetLobbyRevision();
        const auto b = Join(room, "Bob");
        EXPECT_GT(room.GetLobbyRevision(), revision);

        revision = room.GetLobbyRevision();
        EXPECT_TRUE(room.ChooseSeat(b.player, 1).has_value());  // the same seat
        EXPECT_FALSE(room.ChooseSeat(b.player, 0).has_value());
        EXPECT_FALSE(room.ChooseSeat(b.player, 9).has_value());
        EXPECT_FALSE(room.RemoveMember(55));
        EXPECT_FALSE(room.AddMember("", kNow).has_value());
        EXPECT_EQ(room.GetLobbyRevision(), revision);

        EXPECT_TRUE(room.ChooseSeat(b.player, 2).has_value());
        EXPECT_GT(room.GetLobbyRevision(), revision);
        revision = room.GetLobbyRevision();
        EXPECT_TRUE(room.RemoveMember(a.player));
        EXPECT_GT(room.GetLobbyRevision(), revision);
    }

    TEST(RoomLobbyTest, TokenLookup) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        const auto b = Join(room, "Bob");
        EXPECT_NE(a.token, b.token);
        EXPECT_EQ(a.token.size(), RoomLimits::kTokenLength);
        EXPECT_EQ(room.FindByToken(a.token), a.player);
        EXPECT_EQ(room.FindByToken(b.token), b.player);
        EXPECT_FALSE(room.FindByToken("").has_value());
        EXPECT_FALSE(room.FindByToken("0123456789abcdef0123456789abcdef").has_value());

        // Near misses: one character changed (first, last), a prefix, one character too many
        eastl::string first = a.token;
        first[0] = first[0] == '0' ? '1' : '0';
        eastl::string last = a.token;
        last[last.size() - 1] = last[last.size() - 1] == '0' ? '1' : '0';
        EXPECT_FALSE(room.FindByToken(first).has_value());
        EXPECT_FALSE(room.FindByToken(last).has_value());
        EXPECT_FALSE(room.FindByToken(eastl::string_view(a.token.data(), a.token.size() - 1)).has_value());
        EXPECT_FALSE(room.FindByToken(eastl::string(a.token) + "0").has_value());
    }

    TEST(RoomLobbyTest, ARemovedMembersTokenNoLongerWorks) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        Join(room, "Bob");
        EXPECT_TRUE(room.RemoveMember(a.player));
        EXPECT_FALSE(room.FindByToken(a.token).has_value());
    }

    TEST(RoomLobbyTest, SnapshotShowsSeatsAndHost) {
        Room room = MakeRoom();
        const auto a = Join(room, "Ann");
        Join(room, "Bob");
        ASSERT_TRUE(room.ChooseSeat(a.player, 2).has_value());
        const LobbySnapshot lobby = room.GetLobbySnapshot();
        EXPECT_EQ(lobby.room, "ABCDE");
        EXPECT_EQ(lobby.game, "fake");
        EXPECT_EQ(lobby.phase, RoomPhase::Lobby);
        ASSERT_EQ(lobby.seats.size(), 4u);
        EXPECT_EQ(lobby.hostSeat, 2);
        EXPECT_EQ(lobby.seats[0].kind, SeatKind::Empty);
        EXPECT_EQ(lobby.seats[1].kind, SeatKind::Human);
        EXPECT_EQ(lobby.seats[1].name, "Bob");
        EXPECT_TRUE(lobby.seats[1].connected);
        EXPECT_EQ(lobby.seats[2].name, "Ann");
        EXPECT_EQ(lobby.seats[3].kind, SeatKind::Empty);
        // It is a valid lobby message
        const eastl::string text = MakeLobbyMessage(lobby);
        EXPECT_TRUE(ParseJson(text).has_value());
    }

}  // namespace BECore::Tests
