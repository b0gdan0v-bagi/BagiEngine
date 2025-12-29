#include "EventManager.h"

namespace Core {

    EventManager::EventManager() = default;

    EventManager::~EventManager() = default;

    void EventManager::RegisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        // Проверяем, не зарегистрирован ли уже этот делегат
        auto it = std::find(_providers.begin(), _providers.end(), provider);
        if (it == _providers.end()) {
            _providers.push_back(provider);
        }
    }

    void EventManager::UnregisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        auto it = std::find(_providers.begin(), _providers.end(), provider);
        if (it != _providers.end()) {
            _providers.erase(it);
        }
    }

    void EventManager::ProcessEvents() const {
        // Обрабатываем события от всех делегатов
        for (auto& provider : _providers) {
            if (provider) {
                provider->ProcessEvents();
            }
        }

        // Обновляем очередь событий (обрабатываем все enqueued события)
        Update();
    }

} // namespace Core

