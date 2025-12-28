#pragma once

#include <Core/MainWindow/IMainWindow.h>
#include <Core/Utils/IntrusivePtr.h>
#include <Core/Utils/Singleton.h>
#include <Core/Events/EventManager.h>
#include <Widgets/WidgetManager.h>

namespace Core {

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize();
        void Run();
        void Cleanup();

        static const IntrusivePtr<IMainWindow>& GetMainWindow() {
            return GetInstance()._window;
        }

        static EventManager& GetEventManager() {
            return GetInstance()._eventManager;
        }

        void SetMainWindow(IntrusivePtr<IMainWindow> window);

    private:

        void StopApplication();

        IntrusivePtr<IMainWindow> _window;
        WidgetManager _widgetManager;
        EventManager _eventManager;
        bool _isRunning = false;
    };
}  // namespace Core
