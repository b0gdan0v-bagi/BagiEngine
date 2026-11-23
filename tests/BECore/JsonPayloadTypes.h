#pragma once

#include <BECore/Control/JsonPayload.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // Reflected test data of the JSON payload leaf (Control/JsonPayload.h). The .gen.hpp is included in JsonPayloadTest.cpp only.
    // Fields are declared out of alphabetical order on purpose: the generated writer sorts the keys.

    namespace JsonPayloadTestData {

        /// A payload between two keys
        struct PayloadBetween {
            BE_CLASS(PayloadBetween)

            BE_REFLECT_FIELD String zulu;
            BE_REFLECT_FIELD JsonPayload middle;
            BE_REFLECT_FIELD String alpha;
        };

        /// A payload that may be missing, next to one that may not
        struct PayloadOptional {
            BE_CLASS(PayloadOptional)

            BE_REFLECT_FIELD uint64_t version = 0;
            BE_REFLECT_FIELD Optional<JsonPayload> maybe;
            BE_REFLECT_FIELD JsonPayload always;
        };

        /// A row of a list: two fields, so an element stays an object
        struct PayloadRow {
            BE_CLASS(PayloadRow)

            BE_REFLECT_FIELD JsonPayload result;
            BE_REFLECT_FIELD bool ok = false;
        };

        struct PayloadRows {
            BE_CLASS(PayloadRows)

            BE_REFLECT_FIELD Vector<PayloadRow> rows;
            BE_REFLECT_FIELD Optional<PayloadRow> last;
        };

        struct PayloadBase {
            BE_CLASS(PayloadBase)

            BE_REFLECT_FIELD String name;
            BE_REFLECT_FIELD JsonPayload id;
        };

        /// Its keys interleave with the base's: `data` sorts between `id` and `name`'s neighbours
        struct PayloadDerived : PayloadBase {
            BE_CLASS(PayloadDerived)

            BE_REFLECT_FIELD JsonPayload data;
            BE_REFLECT_FIELD int32_t zeta = 0;
        };

    }  // namespace JsonPayloadTestData

}  // namespace BECore
