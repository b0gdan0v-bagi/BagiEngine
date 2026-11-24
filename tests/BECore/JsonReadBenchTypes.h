#pragma once

#include <BECore/PoolString/PoolString.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore::Tests {

    // The shapes of the messages JsonReadBenchTest reads: a control request and two room messages

    struct ReadBenchControlParams {
        BE_CLASS(ReadBenchControlParams)

        BE_REFLECT_FIELD String action;
        BE_REFLECT_FIELD PoolString target;
        BE_REFLECT_FIELD double x = 0.0;
        BE_REFLECT_FIELD double y = 0.0;
        BE_REFLECT_FIELD int32_t button = 0;
        BE_REFLECT_FIELD bool pressed = false;
        BE_REFLECT_FIELD String note;
    };

    struct ReadBenchControlRequest {
        BE_CLASS(ReadBenchControlRequest)

        BE_REFLECT_FIELD int64_t id = 0;
        BE_REFLECT_FIELD String method;
        BE_REFLECT_FIELD ReadBenchControlParams params;
    };

    struct ReadBenchOptions {
        BE_CLASS(ReadBenchOptions)

        BE_REFLECT_FIELD bool fast = false;
        BE_REFLECT_FIELD int32_t rounds = 0;
    };

    struct ReadBenchRoomListing {
        BE_CLASS(ReadBenchRoomListing)

        BE_REFLECT_FIELD String room;
        BE_REFLECT_FIELD String host;
        BE_REFLECT_FIELD int32_t taken = 0;
        BE_REFLECT_FIELD int32_t seats = 0;
        BE_REFLECT_FIELD bool started = false;
        BE_REFLECT_FIELD ReadBenchOptions options;
    };

    struct ReadBenchRoomList {
        BE_CLASS(ReadBenchRoomList)

        BE_REFLECT_FIELD int32_t v = 0;
        BE_REFLECT_FIELD String type;
        BE_REFLECT_FIELD String game;
        BE_REFLECT_FIELD Vector<ReadBenchRoomListing> rooms;
        BE_REFLECT_FIELD bool more = false;
    };

    struct ReadBenchLobbySeat {
        BE_CLASS(ReadBenchLobbySeat)

        BE_REFLECT_FIELD int32_t seat = 0;
        BE_REFLECT_FIELD String kind;
        BE_REFLECT_FIELD String name;
        BE_REFLECT_FIELD bool connected = false;
    };

    struct ReadBenchLobby {
        BE_CLASS(ReadBenchLobby)

        BE_REFLECT_FIELD int32_t v = 0;
        BE_REFLECT_FIELD String type;
        BE_REFLECT_FIELD String room;
        BE_REFLECT_FIELD String game;
        BE_REFLECT_FIELD String phase;
        BE_REFLECT_FIELD int32_t host = 0;
        BE_REFLECT_FIELD Vector<ReadBenchLobbySeat> seats;
        BE_REFLECT_FIELD ReadBenchOptions options;
    };

}  // namespace BECore::Tests
