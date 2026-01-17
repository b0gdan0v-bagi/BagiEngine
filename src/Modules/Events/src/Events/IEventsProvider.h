#pragma once

namespace BECore {

    class IEventsProvider : public RefCounted {
    public:
        IEventsProvider() = default;
        ~IEventsProvider() override = default;

        // Обработка событий из внешнего источника
        // Должен преобразовать их в EnTT события и отправить через статические методы событий (например, QuitEvent::Emit())
        virtual void ProcessEvents() = 0;

        // Инициализация делегата
        virtual bool Initialize() = 0;

        // Очистка ресурсов
        virtual void Destroy() = 0;
    };

} // namespace BECore

