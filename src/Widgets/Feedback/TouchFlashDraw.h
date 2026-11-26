#pragma once

#include <imgui.h>

namespace BECore {

    /// A translucent fill of the accent colour over the last ImGui item, as strong as @p alpha (TouchHighlights) says; nothing for 0. Needs a frame in progress
    void FillItemWithTouchFlash(float alpha);

    /// The same over the rect from @p min to @p max in screen coordinates
    void FillRectWithTouchFlash(const ImVec2& min, const ImVec2& max, float alpha);

}  // namespace BECore
