#pragma once

#include <Core/Utils/PassKey.h>
#include <functional>
#include <vector>

namespace Core {
    struct BaseEvent;

    // Менеджер для автоматической регистрации событий
    class EventsQueueRegistry {
    public:
        using UpdateFunction = std::function<void()>;
        
        static void Register(UpdateFunction updateFunc, PassKey<BaseEvent>) {
            GetInstance()._updateFunctions.push_back(std::move(updateFunc));
        }
        
        static void UpdateAll() {
            for (auto& func : GetInstance()._updateFunctions) {
                func();
            }
        }
        
    private:
        static EventsQueueRegistry& GetInstance() {
            static EventsQueueRegistry instance;
            return instance;
        }
        
        std::vector<UpdateFunction> _updateFunctions;
    };

}  // namespace Core

