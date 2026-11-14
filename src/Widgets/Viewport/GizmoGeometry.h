#pragma once

#include <BECore/Math/Vec2.h>

namespace BECore::Gizmo {

    /// What a drag of the gizmo does to the selected nodes
    CORE_ENUM(Tool, uint8_t, Translate, Rotate, Scale)

    /// The axes of the translate gizmo: World's are the screen's, Local's are the turned axes of one selected node (a group always uses World's)
    CORE_ENUM(Space, uint8_t, World, Local)

    /// A part of the gizmo that a press can grab. The corners run as the quad of a node does (top left, top right, bottom right, bottom left); Edge i is the middle of the
    /// side from corner i to corner i + 1 (top, right, bottom, left)
    CORE_ENUM(Handle, uint8_t, None, AxisX, AxisY, Free, Ring, Corner0, Corner1, Corner2, Corner3, Edge0, Edge1, Edge2, Edge3)

    inline constexpr float kDefaultPositionStep = 8.0f;  ///< scene units
    inline constexpr float kDefaultAngleStep = 15.0f;    ///< degrees
    inline constexpr float kDefaultScaleStep = 0.1f;     ///< a factor on the start size

    /// Which kinds of snapping are on, and the step of each; a step of 0 or less switches that kind off
    struct SnapSettings {
        bool position = false;
        bool angle = false;
        bool scale = false;
        float positionStep = kDefaultPositionStep;
        float angleStep = kDefaultAngleStep;
        float scaleStep = kDefaultScaleStep;
    };

    /// @p snap with the three switches flipped when @p modifier (Ctrl) is held
    [[nodiscard]] SnapSettings WithSnapModifier(const SnapSettings& snap, bool modifier);

    /// @p value rounded to the nearest multiple of @p step (halves away from zero); @p value itself when @p step is 0 or less
    [[nodiscard]] float SnapValue(float value, float step);

    /// @p degrees brought into (-180, 180]
    [[nodiscard]] float NormalizeAngle(float degrees);

    /// @p degrees snapped to a multiple of @p step (off when @p step is 0 or less) and brought into (-180, 180]
    [[nodiscard]] float SnapAngle(float degrees, float step);

    /// @p point turned about @p center by @p degrees, clockwise on a screen whose y points down
    [[nodiscard]] Vec2 TurnAbout(Vec2 point, Vec2 center, float degrees);

    /// The corners in the order top left, top right, bottom right, bottom left of the unturned rect
    using Quad = Array<Vec2, 4>;

    /// The fields of a TransformComponent that a gizmo moves: scene units, degrees clockwise on the screen, y down, the pivot normalized in the rect
    struct Item {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
        float rotation = 0.0f;
        float pivotX = 0.5f;
        float pivotY = 0.5f;

        [[nodiscard]] constexpr bool operator==(const Item&) const = default;
    };

    /// The point the item turns about: (x + pivotX * width, y + pivotY * height)
    [[nodiscard]] Vec2 PivotPoint(const Item& item);

    /// The rect of @p item with its rotation applied
    [[nodiscard]] Quad TurnedQuad(const Item& item);

    /// The axis-aligned bounds of the turned quads of @p items; an empty rect for none
    [[nodiscard]] Rect Bounds(Span<const Item> items);

    /// Where a gizmo sits: the pivot point of one item, the centre of the bounds of a group; (0, 0) for none
    [[nodiscard]] Vec2 Pivot(Span<const Item> items);

    /// Sizes of the gizmo in screen pixels; a handle keeps its size at any zoom through the `unitsPerScreenPixel` of the functions below
    inline constexpr float kArrowLength = 72.0f;      ///< from the centre to the tip of an axis arrow
    inline constexpr float kCenterHalfSize = 7.0f;    ///< half the side of the free-move square
    inline constexpr float kRingRadius = 60.0f;       ///< of the rotate ring
    inline constexpr float kRingHitHalfWidth = 6.0f;  ///< how far from the ring line a press still grabs it
    inline constexpr float kHandleHitRadius = 8.0f;   ///< of an arrow (to its line), a corner and an edge handle (to its point)

    /// The place and the axes of a gizmo over a selection
    struct Frame {
        Vec2 origin;           ///< the centre of the gizmo: Pivot() of the items
        float angle = 0.0f;    ///< degrees: how much the axes are turned (0 for World; the item's rotation for Local with one item; 0 for a group)
        Quad box;              ///< where the scale handles sit: the turned quad of one item, the corners of the bounds of a group
        bool isGroup = false;  ///< more than one item: the scale tool offers the corners only
    };

    /// The frame of @p items (empty items give a frame at (0, 0))
    [[nodiscard]] Frame MakeFrame(Span<const Item> items, Space space);

    /// The unit vector of the frame's X axis (to the right when unturned) and of its Y axis (down when unturned)
    [[nodiscard]] Vec2 AxisX(const Frame& frame);
    [[nodiscard]] Vec2 AxisY(const Frame& frame);

    [[nodiscard]] constexpr bool IsCorner(Handle handle) {
        return handle >= Handle::Corner0 && handle <= Handle::Corner3;
    }

    [[nodiscard]] constexpr bool IsEdge(Handle handle) {
        return handle >= Handle::Edge0 && handle <= Handle::Edge3;
    }

    /// The place of a corner or an edge handle in the rect of an item, each axis 0, 0.5 or 1: Corner0 is (0, 0), Corner2 (1, 1), Edge1 (1, 0.5); (0.5, 0.5) for any other handle
    [[nodiscard]] Vec2 HandleAnchor(Handle handle);

    /// The radius of the rotate ring in scene units
    [[nodiscard]] float RingRadius(float unitsPerScreenPixel);

    /// Where @p handle is drawn: the tip of an arrow, the centre for Free, a corner or the middle of a side of the box, and for Ring the point of the ring on the X axis
    [[nodiscard]] Vec2 HandlePosition(const Frame& frame, Handle handle, float unitsPerScreenPixel);

    /**
     * @brief The handle of @p tool under @p point (scene units), or None.
     *
     * Translate: the free-move square wins over the arrows, and of the two arrows the one nearer to the point (X on a tie). Rotate: the ring. Scale: the nearest of the
     * corners and edge midpoints within kHandleHitRadius (a corner on a tie); a group has the corners only.
     */
    [[nodiscard]] Handle HitTest(const Frame& frame, Tool tool, Vec2 point, float unitsPerScreenPixel);

}  // namespace BECore::Gizmo
