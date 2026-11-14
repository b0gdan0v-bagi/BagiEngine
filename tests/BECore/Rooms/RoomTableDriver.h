#pragma once

#include "RoomSocketClient.h"
#include "WireJudge.h"

#include <RoomServer/DemoCardGame.h>
#include <memory>
#include <set>
#include <vector>

namespace BECore::Tests {

    /// What the socket tests of a room server share: humans at a table of the demo card game, played over real sockets
    /// (RoomServerScenarioTest, RoomServerHostileTest). The judge of the wire is WireJudge.h.

    inline constexpr std::chrono::milliseconds kPlayLimit{30'000};
    inline constexpr int kHumansAtFullTable = 4;
    inline constexpr int kHumansOfAPair = 2;

    using Clients = std::vector<std::unique_ptr<RoomSocketClient>>;

    /// The last message of @p type the client received, or null
    inline const ReceivedMessage* LastOf(const RoomSocketClient& client, StringView type) {
        const auto& received = client.Received();
        for (size_t i = received.size(); i > 0; --i) {
            if (received[i - 1].type == type) {
                return &received[i - 1];
            }
        }
        return nullptr;
    }

    inline size_t CountOf(const RoomSocketClient& client, StringView type) {
        return static_cast<size_t>(std::count_if(client.Received().begin(), client.Received().end(), [&](const ReceivedMessage& message) { return message.type == type; }));
    }

    /// The `view.table` of the last view of the client; null when it has none
    inline const Json* LastTable(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        const Json* inner = view ? FindJsonMember(view->json, "view") : nullptr;
        const Json* table = inner ? FindJsonMember(*inner, "table") : nullptr;
        return table && table->is_array() ? table : nullptr;
    }

    inline int TableSize(const RoomSocketClient& client) {
        const Json* table = LastTable(client);
        return table ? static_cast<int>(table->size()) : -1;
    }

    inline bool HasActions(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        const Json* actions = view ? FindJsonMember(view->json, "actions") : nullptr;
        return actions && actions->is_array() && !actions->empty();
    }

    inline bool SeesTheEnd(const RoomSocketClient& client) {
        const ReceivedMessage* view = LastOf(client, RoomMessageTypes::kView);
        return view && GetJsonBool(view->json, "finished").value_or(false);
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
            action.action = (*FindJsonMember(view->json, "actions"))[0];
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
        std::vector<HandPlayer> players;
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
    inline std::vector<std::string> LeaksOf(const RoomSocketClient& client) {
        std::vector<std::string> leaks;
        for (const ReceivedMessage& message : client.Received()) {
            for (std::string& id : FindLeakedCardIds(std::string_view(message.text.c_str(), message.text.size()), client.seat)) {
                leaks.push_back(std::move(id));
            }
        }
        return leaks;
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
            ExpectCleanWire(*clients[i], ("client " + std::to_string(i)).c_str());
        }
    }

    /// The end of a hand: the last view of the client is `finished` and shows every one of the twelve played cards once
    inline void ExpectAllTwelveCardsPlayed(const RoomSocketClient& client) {
        ASSERT_TRUE(SeesTheEnd(client));
        const Json* table = LastTable(client);
        ASSERT_NE(table, nullptr);
        const std::vector<std::string> ids = FindCardIds(table->dump());
        EXPECT_EQ(ids.size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
        EXPECT_EQ(Set<std::string>(ids.begin(), ids.end()).size(), static_cast<size_t>(DemoCardGame::kTotalPlays));
    }

    /// Frames of @p count identical text messages in one buffer
    inline Vector<uint8_t> RepeatText(const std::string& text, int count) {
        const auto frame = EncodeTextFrame(FromStd(text), kClientMask);
        Vector<uint8_t> batch;
        for (int i = 0; i < count; ++i) {
            batch.insert(batch.end(), frame.data(), frame.data() + frame.size());
        }
        return batch;
    }

    /// Reads frames until the server's close frame; nullopt when none comes
    inline Optional<ServerFrame> ReadUntilClose(WebSocketTestClient& client) {
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
    inline void OpenTable(uint16_t port, StringView game, int humans, Clients& out, const Json& options = Json::object()) {
        for (int i = 0; i < humans; ++i) {
            auto client = RoomSocketClient::Connect(port);
            ASSERT_TRUE(client.has_value());
            const String name = Format("Player{}", i);
            if (i == 0) {
                client->Send(CreateRoomMessage{String(game), name, options});
            } else {
                client->JoinByName(out.front()->roomCode, name);
            }
            ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kJoined).has_value());
            ASSERT_TRUE(client->WaitFor(RoomMessageTypes::kLobby).has_value());
            out.push_back(std::make_unique<RoomSocketClient>(std::move(*client)));
        }
    }

}  // namespace BECore::Tests
