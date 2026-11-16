#pragma once

#include <Events/EventBase.h>

namespace BECore {
    namespace ApplicationEvents {

        struct QuitEvent : public EventBase<QuitEvent> {};
        struct ApplicationCleanUpEvent : public EventBase<ApplicationCleanUpEvent> {};
        /// An editor panel asks for the control API while it is off (the Generate panel's Start the service): the application starts the server
        /// on the default port, or on a free one when that is taken. Nothing when it runs already or the build has no control API
        struct StartControlServerRequestEvent : public EventBase<StartControlServerRequestEvent> {};

    }  // namespace ApplicationEvents
}  // namespace BECore
