#pragma once

namespace BECore {

    /**
     * @brief How a draw combines its color with what is already on the target (`src` has the draw's alpha `a`).
     *
     * Set with IRenderer::SetBlendMode(); every primitive obeys it. The SDL backend implements all three, the Vulkan backend
     * draws with Alpha whatever is set (.claude/rules/rendering-module.md).
     */
    CORE_ENUM(BlendMode, uint8_t,
              Alpha,    ///< straight alpha over the target: `src * a + dst * (1 - a)` (the default)
              Add,      ///< `src * a + dst`: brightens, saturating at white
              Multiply  ///< `src * dst + dst * (1 - a)`: darkens; alpha 0 leaves the target alone
    )

}  // namespace BECore
