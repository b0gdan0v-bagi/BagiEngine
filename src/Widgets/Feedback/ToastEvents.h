#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/EnumUtils.h>
#include <Events/EventBase.h>
#include <cstdint>

namespace BECore {

    CORE_ENUM(ToastLevel, uint8_t, Info, Success, Warning, Error)

    /// Asks the editor to show a toast; any editor code emits it, and without the editor's layout widget nothing listens
    struct EditorToastEvent : public EventBase<EditorToastEvent> {
        ToastLevel level;
        String text;  ///< one line; never a format string

        EditorToastEvent(ToastLevel severity, StringView message) : level(severity), text(message.data(), message.size()) {}
    };

}  // namespace BECore
