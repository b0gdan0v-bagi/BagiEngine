#pragma once

#include <BECore/PoolString/PoolString.h>
#include <Events/EventBase.h>

namespace BECore::InputEvents {

    struct ActionEvent : public EventBase<ActionEvent> {
        PoolString action;
        PoolString value;  ///< what the sender chose with the action; empty for a plain button (a segment of a choice group sets it)

        explicit ActionEvent(PoolString a, PoolString v = {}) : action(a), value(v) {}
    };

}  // namespace BECore::InputEvents
