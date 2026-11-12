#pragma once

#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneNode.h>
#include <BECore/UI/UICanvasComponent.h>

namespace BECore::ScenePick {

    /// What a click picked: the node, and for a click on a canvas element that element too. Both are null for a click on nothing. Valid for the frame only
    struct Hit {
        SceneNode* node = nullptr;
        UIElement* element = nullptr;  ///< null for a plain scene node
    };

    /**
     * @brief The topmost thing under a point of the render output, canvas elements and scene nodes alike.
     *
     * Topmost is the highest Scene::GetDrawRank() among the candidates; on one node a canvas element wins over the node's own rect (the canvas draws after the
     * quad or sprite of its node). A node is skipped with its subtree when it has EditorFlags::Hidden (the draw system skips it too); a locked node can be picked.
     *
     * A scene node is a candidate when it has a TransformComponent with a positive width and height and one more component; its rect is in scene units and its rotation
     * is honoured (the game's hit tests ignore it, the editor does not). A canvas node is a candidate when its canvas is visible and has a layout, and
     * UICanvasLayout::PickElement finds an element at the point.
     *
     * @param outputX,outputY a point in the pixels of the pass that drew the scene last: a canvas takes it through its layout, a scene node through Scene::ScreenToScene*
     */
    Hit PickAt(Scene& scene, float outputX, float outputY);

    /// True when (@p x, @p y) lies in @p rect turned by @p rotationDegrees (clockwise on the screen) about the pivot, a point of the rect from 0 to 1 on each axis. The left and top edges belong to
    /// the rect
    bool PointInRotatedRect(const Rect& rect, float rotationDegrees, float pivotX, float pivotY, float x, float y);

    using Quad = eastl::array<Vec2, 4>;  ///< the corners in the order top left, top right, bottom right, bottom left of the unturned rect

    /// The outlines of a selected node: no heap
    struct Outlines {
        static constexpr size_t kMaxQuads = 2;
        eastl::array<Quad, kMaxQuads> quads;
        size_t count = 0;
    };

    /**
     * @brief The quads that show @p node over the image, in render output pixels of the pass that drew @p scene last.
     *
     * First the rect of its TransformComponent with the rotation applied (taken to output by Scene::GetViewTransform), then, for a node with a canvas that has a layout,
     * the bounds of the canvas (UIPreviewOverlay::CanvasBounds). Empty for a node with neither.
     */
    Outlines SelectionOutlines(const Scene& scene, const SceneNode& node);

    /// The element of the tree of @p canvas that is @p wanted, or null. Compares pointers and never dereferences @p wanted, so a pointer that is stale is safe
    UIElement* FindElement(UICanvasComponent& canvas, const UIElement* wanted);

}  // namespace BECore::ScenePick
