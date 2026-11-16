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

        /// Runs the updates registered when it started. A handler may enqueue a type queued for the first time, which
        /// registers it (and can reallocate the list); that type is first updated by the next call.
        static void UpdateAll() {
            Vector<UpdateFunction>& functions = GetInstance()._updateFunctions;
            const size_t count = functions.size();
            for (size_t i = 0; i < count; ++i) {
                const UpdateFunction func = functions[i];
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
