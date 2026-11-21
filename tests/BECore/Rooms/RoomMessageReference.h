#pragma once

#include <BECore/Control/Json.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Tests {

    /// The room messages built the way `RoomProtocol.cpp` built them before the JsonWriter: a `Json` DOM and DumpJson. The writer's bytes
    /// are compared with these, in the tests and the benchmark.
    namespace RoomMessageReference {

        inline Json MakeEnvelope(StringView type) {
            Json root = Json::object();
            root["v"] = kRoomProtocolVersion;
            root["type"] = MakeJsonString(type);
            return root;
        }

        inline String MakeJoinedMessage(StringView room, PlayerId player, StringView token, int seat, bool host) {
            Json root = MakeEnvelope(RoomMessageTypes::kJoined);
            root["room"] = MakeJsonString(room);
            root["player"] = player;
            root["token"] = MakeJsonString(token);
            root["seat"] = seat;
            root["host"] = host;
            return DumpJson(root);
        }

        inline String MakeLobbyMessage(const LobbySnapshot& lobby) {
            Json root = MakeEnvelope(RoomMessageTypes::kLobby);
            root["room"] = MakeJsonString(lobby.room);
            root["game"] = MakeJsonString(lobby.game);
            root["phase"] = MakeJsonString(GetPhaseName(lobby.phase));
            root["host"] = lobby.hostSeat;
            Json seats = Json::array();
            for (const LobbySeat& seat : lobby.seats) {
                Json entry = Json::object();
                entry["seat"] = seat.seat;
                entry["kind"] = MakeJsonString(GetSeatKindName(seat.kind));
                if (seat.kind == SeatKind::Human) {
                    entry["name"] = MakeJsonString(seat.name);
                    entry["connected"] = seat.connected;
                }
                seats.push_back(std::move(entry));
            }
            root["seats"] = std::move(seats);
            if (lobby.options.is_object() && !lobby.options.empty()) {
                root["options"] = lobby.options;
            }
            return DumpJson(root);
        }

        inline String MakeRoomListMessage(const RoomListMessage& list) {
            Json root = MakeEnvelope(RoomMessageTypes::kRooms);
            root["game"] = MakeJsonString(list.game);
            Json rooms = Json::array();
            for (const RoomListing& listing : list.rooms) {
                Json entry = Json::object();
                entry["room"] = MakeJsonString(listing.room);
                entry["host"] = MakeJsonString(listing.host);
                entry["taken"] = listing.taken;
                entry["seats"] = listing.seats;
                entry["started"] = listing.started;
                if (listing.options.is_object() && !listing.options.empty()) {
                    entry["options"] = listing.options;
                }
                rooms.push_back(std::move(entry));
            }
            root["rooms"] = std::move(rooms);
            root["more"] = list.more;
            return DumpJson(root);
        }

        inline String MakeViewMessage(StringView room, int seat, uint64_t version, bool finished, const Json& view, const Json& actions) {
            Json root = MakeEnvelope(RoomMessageTypes::kView);
            root["room"] = MakeJsonString(room);
            root["seat"] = seat;
            root["version"] = version;
            root["finished"] = finished;
            root["view"] = view;
            root["actions"] = actions;
            return DumpJson(root);
        }

        inline String MakeErrorMessage(StringView code, StringView message, StringView ref = {}) {
            Json root = MakeEnvelope(RoomMessageTypes::kError);
            root["code"] = MakeJsonString(code);
            root["message"] = MakeJsonString(message);
            if (!ref.empty()) {
                root["ref"] = MakeJsonString(ref);
            }
            return DumpJson(root);
        }

        inline String MakeLeftMessage(StringView reason) {
            Json root = MakeEnvelope(RoomMessageTypes::kLeft);
            root["reason"] = MakeJsonString(reason);
            return DumpJson(root);
        }

        inline String MakePongMessage() {
            return DumpJson(MakeEnvelope(RoomMessageTypes::kPong));
        }

    }  // namespace RoomMessageReference

}  // namespace BECore::Tests
