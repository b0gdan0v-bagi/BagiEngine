#pragma once

#include <Events/IEventsProvider.h>

namespace BECore {

    class EventsProviderManager {
    public:
        EventsProviderManager() = default;
        ~EventsProviderManager() = default;

        void RegisterProvider(const IntrusivePtr<IEventsProvider>& provider);
        void UnregisterProvider(const IntrusivePtr<IEventsProvider>& provider);

        void ProcessEvents() const;

    private:
        Vector<IntrusivePtr<IEventsProvider>> _providers;
    };

}  // namespace BECore
