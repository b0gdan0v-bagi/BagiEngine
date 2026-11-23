#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <Rooms/RoomProtocol.h>
#include <Rooms/RoomTypes.h>
#include <cstdint>

namespace BECore {

    // The messages of the room protocol that carry no `Json` payload, written by the generated `WriteJson` (`Reflection/CLAUDE.md`, "The generated writer").
    // Wire structs, not the plain structs of `RoomProtocol.h`: those are aggregates that clients and the fuzz seeds brace-initialise and that the readers fill after
    // checking untrusted input, while a `BE_CLASS` has a virtual and the wire names differ (`kind` is lower case text, `name` and `connected` only for a human).
    // A message with a `Json` payload in the middle of the object (`view`, `action`, `create_room`, `lobby`, `rooms`) stays hand-written in `RoomProtocol.cpp`.

    /// `type` and `v`, which every message has; the generator merges these keys with those of the derived struct, so they land between its own keys in the sorted order
    struct RoomWireEnvelope {
        BE_CLASS(RoomWireEnvelope)

        BE_REFLECT_FIELD String type;
        // `_v`, key `v`: a field named `v` clashes with the visitor parameter of the generated `AcceptPropertyVisitor`
        BE_REFLECT_FIELD int64_t _v = kRoomProtocolVersion;

    protected:
        // The registry makes a default object of every reflected type, so the type text is set by the derived constructor
        RoomWireEnvelope() = default;
        explicit RoomWireEnvelope(StringView messageType) : type(messageType.data(), messageType.size()) {}
    };

    /// `joined`
    struct RoomWireJoined : RoomWireEnvelope {
        BE_CLASS(RoomWireJoined)

        BE_REFLECT_FIELD bool host = false;
        BE_REFLECT_FIELD PlayerId player = 0;
        BE_REFLECT_FIELD String room;
        BE_REFLECT_FIELD int seat = 0;
        BE_REFLECT_FIELD String token;

        RoomWireJoined() : RoomWireEnvelope(RoomMessageTypes::kJoined) {}
    };

    /// `error`: `ref` is the type of the message it answers, absent when none
    struct RoomWireError : RoomWireEnvelope {
        BE_CLASS(RoomWireError)

        BE_REFLECT_FIELD String code;
        BE_REFLECT_FIELD String message;
        BE_REFLECT_FIELD Optional<String> ref;

        RoomWireError() : RoomWireEnvelope(RoomMessageTypes::kError) {}
    };

    /// `left`
    struct RoomWireLeft : RoomWireEnvelope {
        BE_CLASS(RoomWireLeft)

        BE_REFLECT_FIELD String reason;

        RoomWireLeft() : RoomWireEnvelope(RoomMessageTypes::kLeft) {}
    };

    /// `pong`
    struct RoomWirePong : RoomWireEnvelope {
        BE_CLASS(RoomWirePong)

        RoomWirePong() : RoomWireEnvelope(RoomMessageTypes::kPong) {}
    };

    /// One seat of `lobby`: `connected` and `name` only for a human; `kind` is the lower case wire name, not the enum
    struct RoomWireSeat {
        BE_CLASS(RoomWireSeat)

        BE_REFLECT_FIELD Optional<bool> connected;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD Optional<String> name;
        BE_REFLECT_FIELD int seat = 0;
    };

    /// `join_room`: `token` when a member returns, `name` otherwise, never both
    struct RoomWireJoinRoom : RoomWireEnvelope {
        BE_CLASS(RoomWireJoinRoom)

        BE_REFLECT_FIELD String code;
        BE_REFLECT_FIELD Optional<String> name;
        BE_REFLECT_FIELD Optional<String> token;

        RoomWireJoinRoom() : RoomWireEnvelope(RoomMessageTypes::kJoinRoom) {}
    };

    /// `choose_seat`
    struct RoomWireChooseSeat : RoomWireEnvelope {
        BE_CLASS(RoomWireChooseSeat)

        BE_REFLECT_FIELD int64_t seat = 0;

        RoomWireChooseSeat() : RoomWireEnvelope(RoomMessageTypes::kChooseSeat) {}
    };

    /// `list_rooms`
    struct RoomWireListRooms : RoomWireEnvelope {
        BE_CLASS(RoomWireListRooms)

        BE_REFLECT_FIELD String game;

        RoomWireListRooms() : RoomWireEnvelope(RoomMessageTypes::kListRooms) {}
    };

    /// `start`
    struct RoomWireStart : RoomWireEnvelope {
        BE_CLASS(RoomWireStart)

        RoomWireStart() : RoomWireEnvelope(RoomMessageTypes::kStart) {}
    };

    /// `leave`
    struct RoomWireLeave : RoomWireEnvelope {
        BE_CLASS(RoomWireLeave)

        RoomWireLeave() : RoomWireEnvelope(RoomMessageTypes::kLeave) {}
    };

    /// `ping`
    struct RoomWirePing : RoomWireEnvelope {
        BE_CLASS(RoomWirePing)

        RoomWirePing() : RoomWireEnvelope(RoomMessageTypes::kPing) {}
    };

}  // namespace BECore
