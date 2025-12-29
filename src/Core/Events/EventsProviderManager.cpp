#include "EventsProviderManager.h"
#include "Events.h"
#include <algorithm>
#include <vector>

namespace Core {

    void EventsProviderManager::RegisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        // Проверяем, не зарегистрирован ли уже этот провайдер
        auto it = std::find(_providers.begin(), _providers.end(), provider);
        if (it == _providers.end()) {
            _providers.push_back(provider);
        }
    }

    void EventsProviderManager::UnregisterProvider(const IntrusivePtr<IEventsProvider>& provider) {
        if (!provider) {
            return;
        }

        auto it = std::find(_providers.begin(), _providers.end(), provider);
        if (it != _providers.end()) {
            _providers.erase(it);
        }
    }

    void EventsProviderManager::ProcessEvents() const {
        // Обрабатываем события от всех провайдеров
        for (auto& provider : _providers) {
            if (provider) {
                provider->ProcessEvents();
            }
        }

        // Обновляем очереди всех событий
        QuitEvent::Update();
        NewFrameEvent::Update();
        RenderClearEvent::Update();
        RenderPresentEvent::Update();
        ApplicationCleanUpEvent::Update();
        SetRenderDrawColorEvent::Update();
        KeyDownEvent::Update();
        KeyUpEvent::Update();
        MouseButtonDownEvent::Update();
        MouseButtonUpEvent::Update();
        MouseMotionEvent::Update();
        MouseWheelEvent::Update();
        WindowEvent::Update();
        SDLEventWrapper::Update();
    }

} // namespace Core

