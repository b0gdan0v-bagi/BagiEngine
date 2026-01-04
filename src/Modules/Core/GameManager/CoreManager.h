#pragma once

#include <Core/Events/EventsProviderManager.h>
#include <Core/MainWindow/MainWindowManager.h>
#include <Core/Utils/PassKey.h>
#include <Core/Utils/Singleton.h>
#include <Core/Widgets/WidgetManager.h>

namespace Core {

    // Forward declaration
    class Application;

    class CoreManager : public Singleton<CoreManager> {
    public:
        CoreManager() = default;
        ~CoreManager() override = default;

        static WidgetManager& GetWidgetManager() {
            return GetInstance()._widgetManager;
        }

        static EventsProviderManager& GetEventsProviderManager() {
            return GetInstance()._eventsProviderManager;
        }

        static MainWindowManager& GetMainWindowManager() {
            return GetInstance()._mainWindowManager;
        }

        static const IntrusivePtr<IMainWindow>& GetMainWindow();

        // Функция инициализации приложения
        void OnApplicationInit(PassKey<Application>);

        // Функция игрового цикла
        void OnGameCycle(PassKey<Application>) const;

        // Функция деинициализации приложения
        void OnApplicationDeinit(PassKey<Application>);

    private:
        WidgetManager _widgetManager;
        EventsProviderManager _eventsProviderManager;
        MainWindowManager _mainWindowManager;
    };

}  // namespace Core

