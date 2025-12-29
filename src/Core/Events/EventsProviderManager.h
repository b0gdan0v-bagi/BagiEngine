#pragma once

#include <Core/Events/IEventsProvider.h>
#include <Core/Utils/IntrusivePtr.h>
#include <vector>

namespace Core {

    class EventsProviderManager {
    public:
        EventsProviderManager() = default;
        ~EventsProviderManager() = default;

        void RegisterProvider(const IntrusivePtr<IEventsProvider>& provider);
        void UnregisterProvider(const IntrusivePtr<IEventsProvider>& provider);

        void ProcessEvents() const;

    private:
        std::vector<IntrusivePtr<IEventsProvider>> _providers;
    };

}  // namespace Core

