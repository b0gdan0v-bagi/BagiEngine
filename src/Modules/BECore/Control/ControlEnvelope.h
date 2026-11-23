#pragma once

#include <BECore/Control/JsonPayload.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>

namespace BECore {

    // The three envelopes of the wire as reflected structs: the generated `WriteJson` writes the keys in alphabetical order, as a `Json` object keeps them.
    // They are locals of the writer: every `JsonPayload` in them is a view of the client's `id` or of the handler's callback, and the texts are views of the error or the event name of the writer.

    /// The error of a failed call: `error` of the error envelope, and the `error` of a failed entry of `batch`. A view of the `ControlError` it was made
    /// from, which the code that writes it holds (`TextView`: a local of that code)
    struct ControlErrorAnswer {
        BE_CLASS(ControlErrorAnswer)

        BE_REFLECT_FIELD TextView code;
        BE_REFLECT_FIELD TextView message;
    };

    /// The response of a successful call, `{"id":..,"ok":true,"result":..}`
    struct ControlOkEnvelope {
        BE_CLASS(ControlOkEnvelope)

        BE_REFLECT_FIELD JsonPayload id;
        BE_REFLECT_FIELD bool ok = true;
        BE_REFLECT_FIELD JsonPayload result;
    };

    /// The response of a failed call, `{"error":{..},"id":..,"ok":false}`. The error is a copy of the `ControlError`
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
        BE_REFLECT_FIELD TextView event;
    };

}  // namespace BECore
