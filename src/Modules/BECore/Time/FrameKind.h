#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// What the game does in one frame, as GamePause::BeginFrame() decides it
    CORE_ENUM(FrameKind, uint8_t, Running, Stepped, Held)

}  // namespace BECore
