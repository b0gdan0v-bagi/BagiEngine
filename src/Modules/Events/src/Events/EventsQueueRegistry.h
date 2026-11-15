#pragma once

#include <BECore/Types/Types.h>
#include <BECore/Utils/PassKey.h>

namespace BECore {
    struct BaseEvent;

    class EventsQueueRegistry {
    public:
        /// A plain function: MoveOnlyFunction asserts through events, so this header (which every event includes) cannot include it, and an update needs no capture
        using UpdateFunction = void (*)();

        static void Register(UpdateFunction updateFunc, PassKey<BaseEvent>) {
            GetInstance()._updateFunctions.push_back(updateFunc);
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

        Vector<UpdateFunction> _updateFunctions;
    };

}  // namespace BECore
