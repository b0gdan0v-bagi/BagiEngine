#pragma once

#include <Core/Events/IEventsProvider.h>
#include <Core/Utils/IntrusivePtr.h>
#include <entt/entt.hpp>

namespace Core {

    class EventManager {
    public:
        EventManager();
        ~EventManager();

        void RegisterProvider(const IntrusivePtr<IEventsProvider>& Provider);
        void UnregisterProvider(const IntrusivePtr<IEventsProvider>& Provider);

        void ProcessEvents();

        template <typename T, auto Candidate, typename Type>
        void Subscribe(Type* instance) {
            _dispatcher.sink<T>().template connect<Candidate>(instance);
        }

        template <typename T>
        void Emit(T&& event) {
            _dispatcher.trigger(std::forward<T>(event));
        }

        template <typename T>
        void Enqueue(const T& event) {
            _dispatcher.enqueue(event);
        }

        void Update() const {
            _dispatcher.update();
        }

        entt::dispatcher& GetDispatcher() {
            return _dispatcher;
        }

        const entt::dispatcher& GetDispatcher() const {
            return _dispatcher;
        }

    private:
        entt::dispatcher _dispatcher;
        std::vector<IntrusivePtr<IEventsProvider>> _providers;
    };

}  // namespace Core
