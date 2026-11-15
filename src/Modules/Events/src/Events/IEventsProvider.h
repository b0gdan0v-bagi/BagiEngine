#pragma once

#include <BECore/RefCounted/RefCounted.h>

namespace BECore {

    class IEventsProvider : public RefCounted {
    public:
        IEventsProvider() = default;
        ~IEventsProvider() override = default;

        // Turns what the outside source has queued into engine events (e.g. QuitEvent::Emit()).
        virtual void ProcessEvents() = 0;

        virtual bool Initialize() = 0;

        virtual void Destroy() = 0;
    };

}  // namespace BECore
