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
        void StopApplication();

        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

        EventManager& GetEventManager() {
            return _eventManager;
        }

        const EventManager& GetEventManager() const {
            return _eventManager;
        }

    private:
        IntrusivePtr<IMainWindow> _window;
        WidgetManager _widgetManager;
        EventManager _eventManager;
        bool _isRunning = false;
    };
}  // namespace Core
