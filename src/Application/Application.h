#pragma once

#include <Core/MainWindow/IMainWindow.h>
#include <Core/Utils/IntrusivePtr.h>
#include <Core/Utils/Singleton.h>
#include <Core/Events/EventsProviderManager.h>
#include <Widgets/WidgetManager.h>

namespace Core {

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize(PassKey<ApplicationMainAccess>);
        void Run(PassKey<ApplicationMainAccess>);
        void Cleanup(PassKey<ApplicationMainAccess>);

        static const IntrusivePtr<IMainWindow>& GetMainWindow() {
            return GetInstance()._window;
        }

        static EventsProviderManager& GetEventsProviderManager() {
            return GetInstance()._eventsProviderManager;
        }

        void SetMainWindow(IntrusivePtr<IMainWindow> window, PassKey<ApplicationMainAccess>);

    private:

        void StopApplication();

        IntrusivePtr<IMainWindow> _window;
        WidgetManager _widgetManager;
        EventsProviderManager _eventsProviderManager;
        bool _isRunning = false;
    };
}  // namespace Core
