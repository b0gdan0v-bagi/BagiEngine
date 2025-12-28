#pragma once

#include <Core/Utils/RefCounted.h>

namespace Core {

    class IEventsProvider : public RefCounted {
    public:
        IEventsProvider() = default;
        ~IEventsProvider() override = default;

        // Обработка событий из внешнего источника
        // Должен преобразовать их в EnTT события и отправить в EventManager
        virtual void ProcessEvents() = 0;

        // Инициализация делегата
        virtual bool Initialize() = 0;

        // Очистка ресурсов
        virtual void Destroy() = 0;
    };

} // namespace Core

