#pragma once

#include <Events/EventBase.h>

namespace BECore::ClipboardEvents {

    /// The user pasted text (Cmd+V / Ctrl+V, or a paste the browser announced): the text of the clipboard, never empty
    struct PasteEvent : public EventBase<PasteEvent> {
        eastl::string text;

        explicit PasteEvent(eastl::string_view value) : text(value.data(), value.size()) {}
    };

}  // namespace BECore::ClipboardEvents
