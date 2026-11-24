#pragma once

#include <BECore/Control/Json.h>
#include <BECore/Utils/Overload.h>
#include <Rooms/RoomProtocol.h>

namespace BECore::Tests {

    /// The room messages built the way `RoomProtocol.cpp` built them before the JsonWriter: a `Json` DOM and DumpJson. The writer's bytes
    /// are compared with these, in the tests and the benchmark.
    namespace RoomMessageReference {

        /// The DOM of the text a message member holds (`options`, `action`); an empty text is the empty object
        inline Json DocumentOf(const String& text) {
            auto parsed = ParseJson(text.empty() ? StringView("{}") : StringView(text.data(), text.size()));
            return parsed ? std::move(*parsed) : Json();
        }

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
            if (const Json options = DocumentOf(lobby.options); !options.empty()) {
                root["options"] = options;
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
                if (const Json options = DocumentOf(listing.options); !options.empty()) {
                    entry["options"] = options;
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

        inline String MakeClientMessage(const ClientMessage& message) {
            Json root = MakeEnvelope(GetClientMessageType(message));
            std::visit(overload{
                           [&](const CreateRoomMessage& m) {
                               root["game"] = MakeJsonString(m.game);
                               root["name"] = MakeJsonString(m.name);
                               root["options"] = DocumentOf(m.options);
                               if (m.isPublic) {
                                   root["public"] = true;
                               }
                           },
                           [&](const JoinRoomMessage& m) {
                               root["code"] = MakeJsonString(m.code);
                               if (m.IsReconnect()) {
                                   root["token"] = MakeJsonString(m.token);
                               } else {
                                   root["name"] = MakeJsonString(m.name);
                               }
                           },
                           [&](const ChooseSeatMessage& m) { root["seat"] = m.seat; },
                           [&](const ActionMessage& m) {
                               root["version"] = m.version;
                               root["action"] = DocumentOf(m.action);
                           },
                           [](const StartMessage&) {},
                           [](const LeaveMessage&) {},
                           [](const PingMessage&) {},
                           [&](const ListRoomsMessage& m) { root["game"] = MakeJsonString(m.game); },
                       },
                       message);
            return DumpJson(root);
        }

    }  // namespace RoomMessageReference

}  // namespace BECore::Tests
