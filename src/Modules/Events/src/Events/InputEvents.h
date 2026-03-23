#pragma once

#include <BECore/PoolString/PoolString.h>
#include <Events/EventBase.h>

namespace BECore::InputEvents {

    struct ActionEvent : public EventBase<ActionEvent> {
        PoolString action;

        explicit ActionEvent(PoolString a) : action(a) {}
    };

}  // namespace BECore::InputEvents
