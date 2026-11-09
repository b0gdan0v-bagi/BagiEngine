#pragma once

#include <BECore/Scene/IComponent.h>

namespace BECore {

    /**
     * The rect of a node in scene units. @c _rotation turns the node's quad or sprite about a pivot, clockwise on the screen
     * (y points down); the pivot is normalized in the node's rect (0.5, 0.5 is the center). Text, UI and hit tests ignore the rotation.
     */
    class TransformComponent : public IComponent {
        BE_CLASS(TransformComponent)
    public:
        TransformComponent() = default;
        ~TransformComponent() override = default;

        BE_REFLECT_FIELD float _x = 0.0f;
        BE_REFLECT_FIELD float _y = 0.0f;
        BE_REFLECT_FIELD float _width = 1.0f;
        BE_REFLECT_FIELD float _height = 1.0f;
        BE_REFLECT_FIELD float _rotation = 0.0f;  ///< degrees, clockwise on the screen; 0 draws as before
        BE_REFLECT_FIELD float _pivotX = 0.5f;    ///< the pivot of the rotation in the node's rect, 0 left edge to 1 right edge
        BE_REFLECT_FIELD float _pivotY = 0.5f;    ///< 0 top edge to 1 bottom edge
    };

}  // namespace BECore
