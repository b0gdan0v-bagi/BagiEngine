#include "EventsProviderManager.h"

namespace Core {

    void EventsProviderManager::RegisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        // Проверяем, не зарегистрирован ли уже этот провайдер
        auto it = eastl::find(_providers.begin(), _providers.end(), provider);
        if (it == _providers.end()) {
            _providers.push_back(provider);
        }
    }

    void EventsProviderManager::UnregisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        auto it = eastl::find(_providers.begin(), _providers.end(), provider);
        if (it != _providers.end()) {
            _providers.erase(it);
        }
    }

    void EventsProviderManager::ProcessEvents() const {
        for (auto& provider : _providers) {
            if (provider) {
                provider->ProcessEvents();
            }
        }
    }

} // namespace Core

