#include "ControlTouches.h"

#include <BECore/Control/ControlActivity.h>

namespace BECore {

    void ControlTouches::Record(PassKey<ControlService>, ControlClient& client, uint64_t nodeId, StringView component, StringView field) {
        ControlTouch& touch = _slots[(_first + _size) % kCapacity];
        if (_size < kCapacity) {
            ++_size;
        } else {
            _first = (_first + 1) % kCapacity;
        }
        touch.sequence = _nextSequence++;
        touch.nodeId = nodeId;
        AssignControlRecordText(touch.component, component);
        AssignControlRecordText(touch.field, field);
        touch.client = &client;
    }

}  // namespace BECore
