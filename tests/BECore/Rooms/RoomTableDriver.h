#pragma once

#include <BECore/Format/Format.h>
#include <BECore/Rooms/RoomSocketClient.h>
#include <BECore/Rooms/WireJudge.h>
#include <BECore/Types/Types.h>
#include <Common/Require.h>
#include <RoomServer/DemoCardGame.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace BECore::Tests {

    // What the socket tests of a room server share: humans at a table of the demo card game, played over real sockets
    // (RoomServerScenarioTest, RoomServerHostileTest). The judge of the wire is WireJudge.h.

    inline constexpr std::chrono::milliseconds kPlayLimit{30'000};
    inline constexpr int kHumansAtFullTable = 4;
    inline constexpr int kHumansOfAPair = 2;

    using Clients = Vector<UniquePtr<RoomSocketClient>>;

    /// The last message of @p type the client received, or null
    inline const ReceivedMessage* LastOf(const RoomSocketClient& client, StringView type) {
        return LastOfType(client.Received(), type);
    }

    inline size_t CountOf(const RoomSocketClient& client, StringView type) {
        return CountOfType(client.Received(), type);
    }

    /// The `view.table` of the last view of the client; null when it has none
    inline Optional<JsonView> LastTable(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        const Optional<JsonView> inner = view ? view->Find("view") : std::nullopt;
        const Optional<JsonView> table = inner ? FindParam(*inner, "table") : std::nullopt;
        return table && table->IsArray() ? table : std::nullopt;
    }

    inline int TableSize(const RoomSocketClient& client) {
        const Optional<JsonView> table = LastTable(client);
        return table ? static_cast<int>(table->Size()) : -1;
    }

    inline bool HasActions(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        const Optional<JsonView> actions = view ? view->Find("actions") : std::nullopt;
        return actions && actions->IsArray() && actions->Size() > 0;
    }

    inline bool SeesTheEnd(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        return view && view->GetBool("finished");
    }

    /// Reads whatever has arrived
    inline void Drain(RoomSocketClient& client) {
        while (client.Next(std::chrono::milliseconds(1))) {
        }
    }

    /// A human at the table: reads what the server sends and, on its turn, plays the first action of its last view
    class HandPlayer {
    public:
        explicit HandPlayer(RoomSocketClient& client) : _client(client) {}

        void Pump() {
            Drain(_client);
            ActIfItsTurn();
        }

        void ActIfItsTurn() {
            const ReceivedMessage* view = LastOf(_client, RoomMessageTypes::kView);
            if (!view || !HasActions(_client)) {
                return;
            }
            const auto version = static_cast<uint64_t>(view->GetInt("version", 0));
            if (_acted && _actedVersion == version) {
                return;
            }
            ActionMessage action;
            action.version = version;
            action.action = String((*Require(view->Find("actions")).Items().begin()).Raw());
            _client.Send(action);
            _acted = true;
            _actedVersion = version;
        }

    private:
        RoomSocketClient& _client;
        bool _acted = false;
        uint64_t _actedVersion = 0;
    };

    /// Plays the hands of @p clients until each has seen the end of its game; @p alsoPump runs on every round (other sockets to serve)
    inline bool PlayToTheEnd(const Clients& clients, const WaitPump& alsoPump = {}) {
        Vector<HandPlayer> players;
        for (const auto& client : clients) {
            players.emplace_back(*client);
        }
        return WaitUntil(
            [&] {
                for (HandPlayer& player : players) {
                    player.Pump();
                }
                if (alsoPump) {
                    alsoPump();
                }
            },
            [&] { return std::all_of(clients.begin(), clients.end(), [](const auto& client) { return SeesTheEnd(*client); }); }, kPlayLimit);
    }

    /// What the judge says of every text the client received: the ids of the cards it must not have seen
    inline Vector<String> LeaksOf(const RoomSocketClient& client) {
        return LeaksOf(client.Received(), client.seat);
    }

    /// The server sent only unmasked text frames (a binary frame, or a masked one, is a failure of its own), and the judge finds no leak
    inline void ExpectCleanWire(const RoomSocketClient& client, const char* who) {
        EXPECT_EQ(client.GetMaskedFrameCount(), 0u) << who;
        EXPECT_EQ(client.GetBinaryFrameCount(), 0u) << who;
        const auto leaks = LeaksOf(client);
        EXPECT_TRUE(leaks.empty()) << who << " was sent the hidden card " << (leaks.empty() ? "" : leaks.front());
    }

    inline void ExpectCleanWire(const Clients& clients) {
        for (size_t i = 0; i < clients.size(); ++i) {
            ExpectCleanWire(*clients[i], Format("client {}", i).c_str());
        }
    }

    /// The end of a hand: the last view of the client is `finished` and shows every card played in the hand once
    inline void ExpectEveryCardPlayed(const RoomSocketClient& client) {
        ASSERT_TRUE(SeesTheEnd(client));
        const Optional<JsonView> table = LastTable(client);
        ASSERT_TRUE(table.has_value());
        const Vector<String> ids = FindCardIds(String(table->Raw()));
        EXPECT_EQ(ids.size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
        EXPECT_EQ(Set<String>(ids.begin(), ids.end()).size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
    }

    /// True when @p lobby shows a human seat that is not connected
    inline bool HasAwayHuman(const ReceivedMessage& lobby) {
        const Optional<JsonView> seats = lobby.Find("seats");
        if (!seats || !seats->IsArray()) {
            return false;
        }
        for (const JsonView seat : seats->Items()) {
            const Optional<bool> connected = GetParamBool(seat, "connected");
            if (GetParamString(seat, "kind").value_or("") == "human" && connected && !*connected) {
                return true;
            }
        }
        return false;
    }

    /// Frames of @p count identical text messages in one buffer
    inline Vector<uint8_t> RepeatText(const String& text, int count) {
        const auto frame = EncodeTextFrame(text, kClientMask);
        Vector<uint8_t> batch;
        batch.reserve(frame.size() * static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            batch.insert(batch.end(), frame.data(), frame.data() + frame.size());
        }
        return batch;
    }

    /// Reads frames until the server's close frame; nullopt when none comes
    inline Optional<RawFrame> ReadUntilClose(WebSocketTestClient& client) {
        for (;;) {
            const auto frame = client.WaitForFrame();
            if (!frame) {
                return std::nullopt;
            }
            if (frame->opcode == WebSocketOpcode::kClose) {
                return frame;
            }
        }
    }

    /// A room of @p game with @p humans humans, each on its own socket and each with `joined` and `lobby` read; the first is the host.
    /// @p options go to the game in `create_room`. Not started
    inline void OpenTable(uint16_t port, StringView game, int humans, Clients& out, StringView options = "{}") {
        for (int i = 0; i < humans; ++i) {
            auto client = RoomSocketClient::Connect(port);
            ASSERT_TRUE(client.has_value());
            const String name = Format("Player{}", i);
            if (i == 0) {
                client->Send(CreateRoomMessage{String(game), name, String(options)});
            } else {
                client->JoinByName(out.front()->roomCode, name);
            }
            ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kJoined).has_value());
            ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kLobby).has_value());
            out.push_back(MakeUnique<RoomSocketClient>(std::move(*client)));
        }
    }

}  // namespace BECore::Tests
