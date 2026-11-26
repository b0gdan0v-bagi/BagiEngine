#pragma once

#include <Events/EventBase.h>

namespace BECore {

    /// The Field Help panel wrote a help file, so a table read before is stale. Needed besides `ConfigChangedEvent`, which the config manager does
    /// not send for a file made while the program runs
    struct FieldHelpSavedEvent : public EventBase<FieldHelpSavedEvent> {};

}  // namespace BECore
