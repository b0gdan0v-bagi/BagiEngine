#pragma once

namespace BECore {

    // Controls of the editor panels that carry a Lucide icon (`ICON_LC_*` of Widgets/Icons/IconsLucide.h) and say what they
    // do on hover. They are ImGui calls: a frame in progress is required. Nothing here allocates; a label is built in a
    // buffer on the stack.

    /// A square button that shows only the icon. The tooltip carries the word, so say what the click does ("Reload the
    /// config"). @p id is the ImGui id, unique inside the window. The tooltip also shows while the button is disabled.
    /// Returns true on the frame of the click.
    bool IconButton(const char* icon, const char* tooltip, const char* id);

    /// A button with the icon and a word. @p text may end with `##id`. @p tooltip is optional. @p width 0 fits the label, a
    /// negative value is the rest of the line (`-1` like ImGui). Returns true on the frame of the click.
    bool IconTextButton(const char* icon, const char* text, const char* tooltip = nullptr, float width = 0.0f);

}  // namespace BECore
