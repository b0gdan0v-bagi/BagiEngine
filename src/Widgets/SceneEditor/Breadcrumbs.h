#pragma once

#include <BECore/Scene/SceneNode.h>
#include <BECore/Types/Types.h>

namespace BECore {

    /**
     * @brief Draws the path of @p node from the root of its tree down to it, in one row that wraps at the right edge of the window
     *
     * Every ancestor is a link whose item label ends with `##crumb<i>` (0 is the root); the node itself is plain text. A chevron stands before every name but
     * the first and wraps with it. @p ancestors is the caller's, so a settled frame allocates nothing. Needs a frame in progress.
     *
     * @return The ancestor clicked in this frame, or null; the row is drawn to its end, so the caller selects it at once
     */
    [[nodiscard]] SceneNode* DrawBreadcrumbs(const SceneNode& node, Vector<SceneNode*>& ancestors);

}  // namespace BECore
