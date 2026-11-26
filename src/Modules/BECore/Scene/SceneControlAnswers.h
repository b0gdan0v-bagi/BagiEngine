#pragma once

#include <BECore/Memory/ArenaAllocator.h>
#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Reflection/TextView.h>
#include <BECore/Types/Types.h>
#include <cstdint>

namespace BECore {

    // The answers of `scene_tree` (`SceneControl.cpp`). A `TextView` looks at a name that outlives the answer: the pool's, a type's

    /// A node of `scene_tree`
    struct SceneTreeNodeAnswer {
        BE_CLASS(SceneTreeNodeAnswer)

        /// The type names of its components, in their order
        BE_REFLECT_FIELD ArenaVector<TextView> components;
        BE_REFLECT_FIELD int32_t layer = 0;
        BE_REFLECT_FIELD TextView name;
        BE_REFLECT_FIELD ArenaString path;
    };

    struct SceneTreeAnswer {
        BE_CLASS(SceneTreeAnswer)

        /// Depth first, a parent before its children
        BE_REFLECT_FIELD ArenaVector<SceneTreeNodeAnswer> nodes;
        BE_REFLECT_FIELD TextView scene;
        /// True when nodes are left out: the limit was reached, or a node is past the limits of a path
        BE_REFLECT_FIELD bool truncated = false;
    };

}  // namespace BECore
