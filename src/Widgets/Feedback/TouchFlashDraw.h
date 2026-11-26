#pragma once

#include <BECore/Types/Types.h>
#include <imgui.h>

namespace BECore {

    class ControlClient;

    /// A translucent fill of the accent colour over the last ImGui item, as strong as @p alpha (TouchHighlights) says; nothing for 0. Needs a frame in progress
    void FillItemWithTouchFlash(float alpha);

    /// The same over the rect from @p min to @p max in screen coordinates
    void FillRectWithTouchFlash(const ImVec2& min, const ImVec2& max, float alpha);

    /// The dim line "changed: <client>" of a mark, @p field in its tooltip. @p prefix is drawn before it, for a line that does not stand under its field.
    /// All three texts are drawn unformatted. Allocates nothing
    void DrawTouchMark(const ControlClient& client, StringView field, StringView prefix = {});

}  // namespace BECore
