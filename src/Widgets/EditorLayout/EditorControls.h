#pragma once

namespace BECore {

    // Controls of the editor panels that carry a Lucide icon (`ICON_LC_*` of Widgets/Icons/IconsLucide.h) and say what they
    // do on hover. They are ImGui calls: a frame in progress is required. Nothing here allocates; a label is built in a
    // buffer on the stack.

    /// While it lives, what is begun is drawn at @p share of the style's alpha; a share of 1 changes nothing.
    class ScopedAlpha {
    public:
        explicit ScopedAlpha(float share);
        ~ScopedAlpha();
        ScopedAlpha(const ScopedAlpha&) = delete;
        ScopedAlpha& operator=(const ScopedAlpha&) = delete;

    private:
        bool _pushed = false;
    };

    /// `ImGui::Button` whose hover colour blends in and out instead of snapping: from the style's `Button` colour to its `ButtonHovered`, whatever the
    /// caller has pushed as them. A disabled button, one under a ScopedHoverSnap and the colour of a pressed one are ImGui's own. A size of 0 fits the
    /// label, as ImGui's.
    bool BlendedButton(const char* label, float width = 0.0f, float height = 0.0f);

    /// While it lives with @p snap true, a BlendedButton keeps ImGui's own colours: for a filled button, which shows its state by them at once.
    class ScopedHoverSnap {
    public:
        explicit ScopedHoverSnap(bool snap);
        ~ScopedHoverSnap();
        ScopedHoverSnap(const ScopedHoverSnap&) = delete;
        ScopedHoverSnap& operator=(const ScopedHoverSnap&) = delete;

    private:
        bool _snap = false;
    };

    /// Shows @p tooltip while the last item is hovered, also when it is disabled (nothing for a null text).
    void ItemTooltip(const char* tooltip);

    /// A square button that shows only the icon. The tooltip carries the word, so say what the click does ("Reload the
    /// config"). @p id is the ImGui id, unique inside the window. The tooltip also shows while the button is disabled.
    /// Returns true on the frame of the click.
    bool IconButton(const char* icon, const char* tooltip, const char* id);

    /// A button with the icon and a word. @p text may end with `##id`. @p tooltip is optional. @p width 0 fits the label, a
    /// negative value is the rest of the line (`-1` like ImGui). Returns true on the frame of the click.
    bool IconTextButton(const char* icon, const char* text, const char* tooltip = nullptr, float width = 0.0f);

    /// What a panel (or a section of one) shows in place of its content: a large icon, centered, and one line of text below
    /// it in the disabled text colour. The text wraps to the width that is free, never cut. It takes the room it needs and
    /// no more, so it is safe in the middle of a window.
    void EmptyState(const char* icon, const char* text);

}  // namespace BECore
