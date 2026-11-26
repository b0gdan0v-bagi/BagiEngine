#pragma once

namespace BECore {

    struct ComponentFieldDifference;

    /**
     * @brief Marks a field that differs from its default, beside the rect it was drawn in (`left`, `top` to `bottom`, screen coordinates)
     *
     * An accent bar stands left of the field over its height and, for a resettable field outside `BeginDisabled`, a reset arrow at its first row, whose item
     * label ends with `##reset_<field>` and whose tooltip names the default. Both are in the indent left of the field, so the caller indents the inspector by
     * a step; neither takes room in the layout. Needs a frame in progress; allocates nothing.
     *
     * @return True on the frame the arrow was clicked; the caller resets the field
     */
    bool DrawDefaultMark(const ComponentFieldDifference& difference, float left, float top, float bottom);

}  // namespace BECore
