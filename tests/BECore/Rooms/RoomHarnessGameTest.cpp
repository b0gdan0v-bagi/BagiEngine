#include <BECore/Rooms/HubTransport.h>
#include <BECore/Rooms/TestClient.h>
#include <Common/Printers.h>
#include <Common/Require.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

namespace BECore::Tests {

    namespace {

        constexpr StringView kUrl = "ws://hub.test";
        constexpr int kTag = 7;
        constexpr int kFirstSeat = 1;

        /// A game the harnesses do not know. Its views carry every hand, so a check of a test's own has something to find
        class OwnGame : public LeakyDemoCardGame {
        public:
            explicit OwnGame(int tag) : _tag(tag) {}

            [[nodiscard]] int GetTag() const {
                return _tag;
            }

        private:
            int _tag;
        };

        /// A client of a HubWorld without a RoomClient: the transport and what arrived through it
        struct WireClient {
            explicit WireClient(HubWorld& world) : transport(world.Factory(links)()) {
                EXPECT_TRUE(transport->Open(kUrl).has_value());
            }

            void Send(const ClientMessage& message) {
                EXPECT_TRUE(transport->SendText(SerializeClientMessage(message)));
            }

            void Poll() {
                RoomTransportEvent event;
                while (transport->PollEvent(event)) {
                    if (event.kind == RoomTransportEventKind::Text) {
                        received.push_back(ReadReceivedMessage(event.text));
                    }
                }
            }

            [[nodiscard]] Vector<String> Texts() const {
                Vector<String> texts;
                for (const ReceivedMessage& message : received) {
                    texts.push_back(message.text);
                }
                return texts;
            }

            HubClientLinks links;
            UniquePtr<IRoomTransport> transport;
            Vector<ReceivedMessage> received;
        };

    }  // namespace

    TEST(RoomHarnessGameTest, TheHarnessRunsAGameOfItsOwnAndTheObserverJudgesIt) {
        Map<ConnectionId, Vector<String>> seen;
        ISeatedGame* made = nullptr;
        size_t viewsWithOtherHands = 0;
        HubHarness harness(
            HubHarness::MakeSettings(), false, MakeSequence(42),
            [&made](HubHarness& owner, SeatedGameRegistry& registry) {
                EXPECT_TRUE(registry.Register(SeatedGameInfo{"own", OwnGame::kSeatCount}, [&made, &owner] {
                    UniquePtr<ISeatedGame> game = owner.MakeTracked<OwnGame>(kTag);
                    made = game.get();
                    return game;
                }));
            },
            [&seen, &viewsWithOtherHands](HubHarness& owner, const TestClient& client, const ReceivedMessage& message) {
                Vector<String>& texts = seen[client.id];
                EXPECT_EQ(client.received.size(), texts.size()) << "the message is already in `received`";
                texts.push_back(message.text);
                if (message.type == RoomMessageTypes::kJoined) {
                    EXPECT_EQ(client.roomCode, message.GetString("room"));
                    EXPECT_EQ(client.seat, message.GetInt("seat"));
                    EXPECT_EQ(client.token, message.GetString("token"));
                } else if (message.type == RoomMessageTypes::kView) {
                    const auto* game = static_cast<const OwnGame*>(owner.GetSeatedGame(client.roomCode));
                    ASSERT_NE(game, nullptr);
                    viewsWithOtherHands += FindHiddenCards(message.text, client.seat, *game).empty() ? 0 : 1;
                } else if (message.type == RoomMessageTypes::kLeft) {
                    EXPECT_FALSE(client.roomCode.empty());
                    EXPECT_GE(client.seat, 0);
                }
            });

        const ConnectionId stranger = harness.Connect();
        harness.Create(stranger, "S", "fake");
        EXPECT_EQ(harness.Client(stranger).LastErrorCode(), RoomErrors::kUnknownGame);

        const Vector<ConnectionId> ids = harness.MakeLobby(2, "own", Format("{{\"firstSeat\":{}}}", kFirstSeat).c_str());
        const String room = harness.Client(ids[0]).roomCode;
        harness.Start(ids[0]);

        ASSERT_NE(made, nullptr);
        ASSERT_EQ(harness.GetSeatedGame(room), made);
        EXPECT_EQ(harness.GetGame(room), nullptr);
        EXPECT_EQ(static_cast<const OwnGame*>(made)->GetTag(), kTag);
        EXPECT_EQ(static_cast<const OwnGame*>(made)->GetTurn(), kFirstSeat);
        EXPECT_EQ(viewsWithOtherHands, ids.size());
        EXPECT_EQ(harness.GetLeakCount(), 0u);

        for (const ConnectionId id : ids) {
            harness.Leave(id);
        }
        EXPECT_EQ(harness.GetSeatedGame(room), nullptr) << "the game of a closed room is still tracked";

        EXPECT_EQ(seen.size(), harness.Clients().size());
        for (const auto& [id, client] : harness.Clients()) {
            Vector<String> texts;
            for (const ReceivedMessage& message : client.received) {
                texts.push_back(message.text);
            }
            EXPECT_EQ(seen[id], texts);
        }
    }

    TEST(RoomHarnessGameTest, TheWorldRunsAGameOfItsOwnAndTheObserverSeesWhatEveryRecipientIsSent) {
        for (const bool useBroadcast : {false, true}) {
            SCOPED_TRACE(useBroadcast ? "broadcast" : "send");
            Map<ConnectionId, Vector<String>> seen;
            HubWorld world(
                HubWorld::MakeSettings(), MakeSequence(11), useBroadcast,
                [](SeatedGameRegistry& registry) { EXPECT_TRUE(registry.Register(SeatedGameInfo{"own", OwnGame::kSeatCount}, [] { return MakeUnique<OwnGame>(kTag); })); },
                [&seen](ConnectionId id, const String& text) { seen[id].push_back(text); });
            WireClient host(world);
            WireClient guest(world);

            host.Send(CreateRoomMessage{"cards", "P0", "{}", false});
            host.Poll();
            EXPECT_EQ(Require(LastOfType(host.received, RoomMessageTypes::kError)).GetString("code"), RoomErrors::kUnknownGame);

            host.Send(CreateRoomMessage{"own", "P0", "{}", false});
            host.Poll();
            JoinRoomMessage join;
            join.code = Require(LastOfType(host.received, RoomMessageTypes::kJoined)).GetString("room");
            join.name = "P1";
            guest.Send(join);
            guest.Poll();

            guest.links.Last().blackhole = true;
            const size_t beforeStart = guest.received.size();
            host.Send(StartMessage{});
            host.Poll();
            guest.Poll();

            EXPECT_NE(LastOfType(host.received, RoomMessageTypes::kView), nullptr);
            EXPECT_EQ(seen[host.links.Last().id], host.Texts());

            EXPECT_EQ(guest.received.size(), beforeStart);
            const Vector<String>& sentToGuest = seen[guest.links.Last().id];
            ASSERT_GT(sentToGuest.size(), beforeStart);
            EXPECT_EQ(Vector<String>(sentToGuest.begin(), sentToGuest.begin() + beforeStart), guest.Texts());
            EXPECT_EQ(ReadReceivedMessage(sentToGuest.back()).type, RoomMessageTypes::kView);
        }
    }

}  // namespace BECore::Tests
