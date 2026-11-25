#pragma once

#include <BECore/Math/Rect.h>
#include <BECore/Math/Vec2.h>
#include <BECore/Types/Types.h>
#include <BECore/UI/UICanvasLayout.h>
#include <BECore/UI/UIElement.h>
#include <BECore/UI/UIElementShapes.h>
#include <BECore/UI/UILayoutContext.h>
#include <Widgets/Viewport/ViewportGizmos.h>
#include <cstdint>
#include <imgui.h>

namespace BECore {

    /**
     * @brief The selected canvas element in the shapes of other devices, outlined over the viewport: the ImGui layer of UIElementShapes. Session state, never stored.
     *
     * The checked devices of UIPreview::GetDevices() and the landscape flag are the toolbar's (IViewportToolbar), handed over every frame with SetShapes(). For each checked shape the live canvas is
     * laid out for that device and given back (UIElementShapesCache: it probes again only when the canvas, the element, the shapes or the layout moved, so a steady frame probes and allocates
     * nothing), and the element's rect is drawn as the same fractions of the image, dashed when it is only approximate (it, or an ancestor, names a region the game published for the live shape).
     * Needs an ImGui frame.
     */
    class ViewportUIShapes {
    public:
        /// The checked devices (bit i: the device i of UIPreview::GetDevices()) and the landscape flag of this frame
        void SetShapes(uint64_t mask, bool landscape) {
            _mask = mask;
            _landscape = landscape;
        }

        /// True when a shape is checked: only then a selected element is probed
        [[nodiscard]] bool AnyChecked() const {
            return _mask != 0;
        }

        /// Draws the outlines of @p element in the checked shapes inside the clip rect the caller pushed; @p outputSize is the target's size, where the image shows the live shape
        void Draw(ImDrawList& drawList, const ViewportFrame& frame, const Vec2& outputSize, UICanvasLayout& layout, const UIElement& element);

        /// The tooltip of the smallest outline under @p mouse (screen units) at the last Draw(): the device, the rect in its pixels and the size of its output; false when there is none
        bool ShowTooltip(const ImVec2& mouse);

        /// No element is selected: forgets the result, which holds a pointer to it, and the outlines of the last frame
        void Release();

    private:
        // One outline of the last Draw(): kept for the tooltip
        struct Drawn {
            size_t device = 0;  ///< the index in UIPreview::GetDevices()
            ImVec2 min;         ///< screen units
            ImVec2 max;
            Rect rect;       ///< in the pixels of the shape
            Vec2 shapeSize;  ///< the size of the shape's output
            bool approximate = false;
        };

        uint64_t _mask = 0;  // the last SetShapes(); bit i: the device i is checked
        bool _landscape = false;
        UIElementShapesCache _cache;
        Vector<UILayoutContext> _contexts;  // one per checked device, kept for the capacity
        Vector<size_t> _devices;            // the device index of each context
        Vector<Drawn> _drawn;
    };

}  // namespace BECore
