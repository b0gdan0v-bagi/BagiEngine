#pragma once

#include <Widgets/Viewport/GizmoGeometry.h>

namespace BECore::Gizmo {

    /// A width or a height never goes below this (scene units) and never flips sign
    inline constexpr float kMinItemSize = 1.0f;

    /**
     * @brief One drag of a gizmo handle: turns the pointer, in scene units, into new values for every item of the selection.
     *
     * No ImGui, no scene: Begin() copies the selection (the start snapshot) and Update() computes the items from the snapshot and the pointer, never from
     * the previous result, so a drag that comes back to its start gives the start values and nothing drifts. The results live in a member vector that keeps
     * its capacity, so a steady drag allocates nothing.
     *
     * - Translate: Free moves by the pointer's offset; AxisX / AxisY keep only the part of the offset along that axis of the frame (the screen's axes in World,
     *   the turned axes of one item in Local). **Snapping** rounds the resulting position of the first item and moves every other item by the same offset
     *   (one rule, so a group keeps its shape): Free rounds x and y; an axis that lies along a screen axis rounds the coordinate it changes; a turned axis rounds
     *   the distance travelled along it.
     * - Rotate: the angle swept around the pivot (the item's own pivot point for one item, the centre of the group's bounds for a group), measured from the
     *   press, so there is no jump at the start. One item changes `rotation` only; in a group every item turns about the group's pivot, so its x and y move too.
     *   **Snapping** rounds the sweep (not the absolute angle) to `angleStep`; the new rotation is brought into (-180, 180].
     * - Scale: one item resizes by a corner or an edge handle with the opposite corner or edge fixed in the item's turned frame (x and y are adjusted for the
     *   rotation and the pivot); an edge changes one axis, a corner both. `keepAspect` makes the change uniform (an edge then scales the other axis about its
     *   middle). A group scales uniformly about its pivot, positions and sizes alike, by the ratio of the pointer's distance from the pivot to its distance
     *   at the press. **Snapping** rounds the factor on the start size (per axis, or the one uniform factor) to a multiple of `scaleStep`.
     *
     * The snap switches come from Begin(); Update()'s @c snapModifier (Ctrl held) inverts all three for that update.
     */
    class GizmoDrag {
    public:
        /// Starts a drag; nothing starts (IsActive() stays false) for no items or Handle::None
        void Begin(Tool tool, Space space, Handle handle, eastl::span<const Item> items, Vec2 point, const SnapSettings& snap);

        /// The items for the pointer at @p point, in the order of Begin()'s items; empty when no drag is active. Valid until the next call
        [[nodiscard]] eastl::span<const Item> Update(Vec2 point, bool snapModifier, bool keepAspect = false);

        /// Ends the drag and returns the start snapshot, valid until the next Begin()
        [[nodiscard]] eastl::span<const Item> Cancel();

        /// Ends the drag and keeps the last result as it is
        void End();

        [[nodiscard]] bool IsActive() const {
            return _active;
        }

        [[nodiscard]] eastl::span<const Item> Start() const {
            return {_start.data(), _start.size()};
        }

        /// The result of the last Update() (the start values before the first one)
        [[nodiscard]] eastl::span<const Item> Current() const {
            return {_result.data(), _result.size()};
        }

    private:
        void ApplyTranslate(Vec2 point, const SnapSettings& snap);
        void ApplyRotate(Vec2 point, const SnapSettings& snap);
        void ApplyScale(Vec2 point, const SnapSettings& snap, bool keepAspect);
        void ApplyScaleOne(Vec2 point, const SnapSettings& snap, bool keepAspect);
        void ApplyScaleGroup(Vec2 point, const SnapSettings& snap);

        bool _active = false;
        Tool _tool = Tool::Translate;
        Handle _handle = Handle::None;
        SnapSettings _snap;
        Vec2 _startPoint;
        Frame _frame;
        Vec2 _pivot;
        eastl::vector<Item> _start;
        eastl::vector<Item> _result;
    };

}  // namespace BECore::Gizmo
