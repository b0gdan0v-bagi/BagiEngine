#pragma once

#include <BECore/Utils/EnumUtils.h>
#include <cstdint>

namespace BECore {

    /// What a content file is to the editor and to a field that takes one; `None` is no asset
    CORE_ENUM(AssetKind, uint8_t, None, Texture, Font, Sound, Scene, Node, UiDocument, Config, Other)

}  // namespace BECore
