#pragma once

#include <BECore/Control/JsonPayload.h>
#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Types/Types.h>

namespace BECore {

    // The three envelopes of the wire as reflected structs: the generated `WriteJson` writes the keys in alphabetical order, as a `Json` object keeps them.
    // They are locals of the writer: every `JsonPayload` in them is a view of the client's `id` or of the handler's callback, and the strings are on the scratch arena
    // of the scope the writer opened.

    /// The error of a failed call: `error` of the error envelope, and the `error` of a failed entry of `batch`. On the scratch arena like every answer
    /// struct (`Reflection/CLAUDE.md`, "Answers on the scratch arena"): a local of the code that writes it, filled with `ArenaString(text.data(), text.size())`
    struct ControlErrorAnswer {
        BE_CLASS(ControlErrorAnswer)

        BE_REFLECT_FIELD ArenaString code;
        BE_REFLECT_FIELD ArenaString message;
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
        BE_REFLECT_FIELD ArenaString event;
    };

}  // namespace BECore
