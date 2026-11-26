#pragma once

#include <BECore/Math/IVec2.h>
#include <BECore/Math/Rect.h>
#include <BECore/PropertyVisitorContainersTest.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/Types/Types.h>

namespace BECore::Tests {

    /// What the shipped components have no field of: containers, attributes that forbid a write, a map of math values, keys a path has to quote
    class RemoteProbeComponent : public IComponent {
        BE_CLASS(RemoteProbeComponent)
    public:
        RemoteProbeComponent() = default;
        ~RemoteProbeComponent() override = default;

        BE_REFLECT_FIELD BE_FIELD_ATTRS(readonly) int32_t _level = 3;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(range(0, 100), step(5), tooltip("Share of the whole")) float _percent = 50.0f;
        BE_REFLECT_FIELD IVec2 _cell{2, 3};
        BE_REFLECT_FIELD Map<String, Rect> _areas;
        BE_REFLECT_FIELD VisitorContainersTestData::ContainerFields _containers;
    };

}  // namespace BECore::Tests
