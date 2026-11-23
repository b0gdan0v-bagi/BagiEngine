#pragma once

#include <BECore/Control/JsonPayload.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    // The three envelopes of the wire as reflected structs: the generated `WriteJson` writes the keys in alphabetical order, as a `Json` object keeps them.
    // They are locals of the writer: every `JsonPayload` in them is a view of the client's `id` or of the handler's callback.

    /// The error of a failed call: `error` of the error envelope, and the `error` of a failed entry of `batch`
    struct ControlErrorAnswer {
        BE_CLASS(ControlErrorAnswer)

        BE_REFLECT_FIELD String code;
        BE_REFLECT_FIELD String message;
    };

    /// The response of a successful call, `{"id":..,"ok":true,"result":..}`
    struct ControlOkEnvelope {
        BE_CLASS(ControlOkEnvelope)

        BE_REFLECT_FIELD JsonPayload id;
        BE_REFLECT_FIELD bool ok = true;
        BE_REFLECT_FIELD JsonPayload result;
    };

    /// The response of a failed call, `{"error":{..},"id":..,"ok":false}`. The errors are the cold path: `code` and `message` are copied
    struct ControlErrorEnvelope {
        BE_CLASS(ControlErrorEnvelope)

        BE_REFLECT_FIELD ControlErrorAnswer error;
        BE_REFLECT_FIELD JsonPayload id;
        BE_REFLECT_FIELD bool ok = false;
    };

    /// The event of `ControlService::Publish`, `{"data":..,"event":..}`
    struct ControlPushEnvelope {
        BE_CLASS(ControlPushEnvelope)

        BE_REFLECT_FIELD JsonPayload data;
        BE_REFLECT_FIELD String event;
    };

}  // namespace BECore
