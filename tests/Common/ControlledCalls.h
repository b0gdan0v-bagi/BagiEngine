#pragma once

#include <BECore/Control/ControlTypes.h>
#include <BECore/Control/JsonView.h>
#include <BECore/Types/Types.h>
#include <Games/GameControl.h>
#include <expected>

namespace BECore::Tests {

    /// ControlledAction() for a test that looks only at what the action did: the outcome text is thrown away
    inline std::expected<void, ControlError> ControlledAction(IControllableGame& game, JsonView action) {
        String outcome;
        return ControlledAction(game, action, outcome);
    }

}  // namespace BECore::Tests
