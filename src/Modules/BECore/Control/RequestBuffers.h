#pragma once

#include <BECore/Control/JsonTape.h>
#include <BECore/Types/Types.h>

namespace BECore {

    class ControlService;

    /// The text of one request and its tape: what a `ControlCall` reads its `params` from. Kept together because the tape borrows the text, so the
    /// text must not move or change while a view of the tape is used (a `String` that is not touched stays where it is, also when it is short)
    struct RequestBuffers {
        String text;
        JsonTape tape;
    };

    /// Gives the buffers back to the free list of the service that made them, from whichever thread the request or the call dies on
    struct RequestBuffersDeleter {
        ControlService* service = nullptr;

        void operator()(RequestBuffers* buffers) const;
    };

    using RequestBuffersPtr = UniquePtr<RequestBuffers, RequestBuffersDeleter>;

}  // namespace BECore
