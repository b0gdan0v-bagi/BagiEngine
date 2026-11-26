#pragma once

#include <BECore/Reflection/ReflectionMarkers.h>
#include <BECore/Scene/SceneNode.h>
#include <cstdint>

namespace BECore::Tests {

    /// A SceneNode subclass with a field of its own, which the writers of a node know nothing of.
    /// Its generated code is included by SceneNodeInstanceTest.cpp, the one of its users that every build compiles.
    class TaggedTestNode : public SceneNode {
        BE_CLASS(TaggedTestNode)
    public:
        BE_REFLECT_FIELD int32_t tag = 0;
    };

}  // namespace BECore::Tests
