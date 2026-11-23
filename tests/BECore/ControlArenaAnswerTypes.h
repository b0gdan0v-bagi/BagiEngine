#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // Answer shapes of the arena rule (strings and lists on the thread's scratch arena) and their heap twins with the same field names:
    // the twin is the oracle of the bytes. The texts a test puts in them are longer than the small-string buffer, so each one takes arena memory.

    namespace ControlArenaTestData {

        struct ArenaRow {
            BE_CLASS(ArenaRow)

            BE_REFLECT_FIELD ArenaString label;
            BE_REFLECT_FIELD int32_t rank = 0;
        };

        struct ArenaAnswer {
            BE_CLASS(ArenaAnswer)

            BE_REFLECT_FIELD ArenaString text;
            BE_REFLECT_FIELD Optional<ArenaString> note;
            BE_REFLECT_FIELD ArenaVector<ArenaRow> rows;
        };

        struct HeapRow {
            BE_CLASS(HeapRow)

            BE_REFLECT_FIELD String label;
            BE_REFLECT_FIELD int32_t rank = 0;
        };

        struct HeapAnswer {
            BE_CLASS(HeapAnswer)

            BE_REFLECT_FIELD String text;
            BE_REFLECT_FIELD Optional<String> note;
            BE_REFLECT_FIELD Vector<HeapRow> rows;
        };

    }  // namespace ControlArenaTestData

}  // namespace BECore
